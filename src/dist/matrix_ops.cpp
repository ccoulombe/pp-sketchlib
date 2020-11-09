/*
 *
 * matrix_ops.cpp
 * Distance matrix transformations
 *
 */
#include <vector>
#include <numeric>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <string>
#include <omp.h>

#include "api.hpp"

// Prototypes for cppying functions run in threads
void square_block(const Eigen::VectorXf& longDists,
                  NumpyMatrix& squareMatrix,
                  const size_t n_samples,
                  const size_t start,
                  const size_t offset,
                  const size_t max_elems);

void rectangle_block(const Eigen::VectorXf& longDists,
                  NumpyMatrix& squareMatrix,
                  const size_t nrrSamples,
                  const size_t nqqSamples,
                  const size_t start,
                  const size_t max_elems);

//https://stackoverflow.com/a/12399290
std::vector<long> sort_indexes(const Eigen::VectorXf &v) {

  // initialize original index locations
  std::vector<long> idx(v.size());
  std::iota(idx.begin(), idx.end(), 0);

  std::stable_sort(idx.begin(), idx.end(),
       [&v](long i1, long i2) {return v[i1] < v[i2];});

  return idx;
}

sparse_coo sparsify_dists(const NumpyMatrix& denseDists,
                          const float distCutoff,
                          const unsigned long int kNN,
                          const unsigned int num_threads) {
    if (kNN > 0 && distCutoff > 0) {
        throw std::runtime_error("Specify only one of kNN or distCutoff");
    } else if (kNN < 1 && distCutoff < 0) {
        throw std::runtime_error("kNN must be > 1 or distCutoff > 0");
    }

    // ijv vectors
    std::vector<float> dists;
    std::vector<long> i_vec;
    std::vector<long> j_vec;
    if (distCutoff > 0) {
        for (long i = 0; i < denseDists.rows(); i++) {
            for (long j = i + 1; j < denseDists.cols(); j++) {
                if (denseDists(i, j) < distCutoff) {
                    dists.push_back(denseDists(i, j));
                    i_vec.push_back(i);
                    j_vec.push_back(j);
                }
            }
        }
    } else if (kNN >= 1) {
        // Only add the k nearest (unique) neighbours
        // May be >k if repeats, often zeros
        for (long i = 0; i < denseDists.rows(); i++) {
            unsigned long unique_neighbors = 0;
            float prev_value = -1;
            for (auto j : sort_indexes(denseDists.row(i))) {
                if (j == i) {
                    continue; // Ignore diagonal which will always be one of the closest
                } else if (unique_neighbors < kNN || denseDists(i, j) == prev_value) {
                    dists.push_back(denseDists(i, j));
                    i_vec.push_back(i);
                    j_vec.push_back(j);
                    if (denseDists(i, j) != prev_value) {
                        unique_neighbors++;
                        prev_value = denseDists(i, j);
                    }
                } else {
                    break;
                }
            }
        }
    }

    return(std::make_tuple(i_vec, j_vec, dists));
}

Eigen::VectorXf assign_threshold(const NumpyMatrix& distMat,
                                 int slope,
                                 float x_max,
                                 float y_max,
                                 unsigned int num_threads) {
    Eigen::VectorXf boundary_test(distMat.rows());

    #pragma omp parallel for schedule(static) num_threads(num_threads)
    for (long row_idx = 0; row_idx < distMat.rows(); row_idx++) {
        float in_tri = 0;
        if (slope == 2) {
            in_tri = distMat(row_idx, 0) * distMat(row_idx, 1) - \
                        (x_max - distMat(row_idx, 0)) * (y_max - distMat(row_idx, 1));
        } else if (slope == 0) {
            in_tri = distMat(row_idx, 0) - x_max;
        } else if (slope == 1) {
            in_tri = distMat(row_idx, 1) - y_max;
        }

        if (in_tri < 0) {
            boundary_test[row_idx] = -1;
        } else if (in_tri == 0) {
            boundary_test[row_idx] = 0;
        } else {
            boundary_test[row_idx] = 1;
        }
    }
    return(boundary_test);
}

NumpyMatrix long_to_square(const Eigen::VectorXf& rrDists,
                            const Eigen::VectorXf& qrDists,
                            const Eigen::VectorXf& qqDists,
                            unsigned int num_threads) {
    // Set up square matrix to move values into
    size_t nrrSamples = rows_to_samples(rrDists);
    size_t nqqSamples = 0;
    if (qrDists.size() > 0) {
        nqqSamples = rows_to_samples(qqDists);
        if (qrDists.size() != nrrSamples * nqqSamples) {
            throw std::runtime_error("Shape of reference, query and ref vs query matrices mismatches");
        }
    }

    // Initialise matrix and set diagonal to zero
    NumpyMatrix squareDists(nrrSamples + nqqSamples, nrrSamples + nqqSamples);
    for (size_t diag_idx = 0; diag_idx < nrrSamples + nqqSamples; diag_idx++) {
        squareDists(diag_idx, diag_idx) = 0;
    }

    // Loop over threads for ref v ref square
    #pragma omp parallel for schedule(static) num_threads(num_threads)
    for (long distIdx = 0; distIdx < rrDists.rows(); distIdx++) {
        unsigned long i = calc_row_idx(distIdx, nrrSamples);
        unsigned long j = calc_col_idx(distIdx, i, nrrSamples);
        squareDists(i, j) = rrDists[distIdx];
        squareDists(j, i) = rrDists[distIdx];
    }

    if (qqDists.size() > 0) {
        #pragma omp parallel for schedule(static) num_threads(num_threads)
        for (long distIdx = 0; distIdx < qqDists.rows(); distIdx++) {
            unsigned long i = calc_row_idx(distIdx, nqqSamples) + nrrSamples;
            unsigned long j = calc_col_idx(distIdx, i - nrrSamples, nqqSamples) + nrrSamples;
            squareDists(i, j) = qqDists[distIdx];
            squareDists(j, i) = qqDists[distIdx];
        }
    }

    // Query vs ref rectangles
    if (qrDists.size() > 0) {
        #pragma omp parallel for schedule(static) num_threads(num_threads)
        for (long distIdx = 0; distIdx < qrDists.rows(); distIdx++) {
            unsigned long i = static_cast<size_t>(distIdx / (float)nqqSamples + 0.001f);
            unsigned long j = distIdx % nqqSamples + nrrSamples;
            squareDists(i, j) = qrDists[distIdx];
            squareDists(j, i) = qrDists[distIdx];
        }
    }

    return squareDists;
}

Eigen::VectorXf square_to_long(const NumpyMatrix& squareDists,
                               const unsigned int num_threads) {
    if (squareDists.rows() != squareDists.cols()) {
        throw std::runtime_error("square_to_long input must be a square matrix");
    }

    long n = squareDists.rows();
    Eigen::VectorXf longDists((n * (n - 1)) >> 1);

    // Each inner loop increases in size linearly with outer index
    // due to reverse direction
    // guided schedules inversely proportional to outer index
    #pragma omp parallel for schedule(guided, 1) num_threads(num_threads)
    for (long i = n - 2; i >= 0; i--) {
        for (long j = i + 1; j < n; j++) {
            longDists(square_to_condensed(i, j, n)) = squareDists(i, j);
        }
    }

    return(longDists);
}
