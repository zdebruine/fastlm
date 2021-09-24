// This file is part of RcppML, a Rcpp Machine Learning library
//
// Copyright (C) 2021 Zach DeBruine <zacharydebruine@gmail.com>
//
// This source code is subject to the terms of the GNU
// Public License v. 2.0.

#ifndef RcppML_project
#define RcppML_project

#ifndef RcppML_common
#include <RcppMLCommon.hpp>
#endif

// solve ax = b given "a", "b", and h.col(sample) giving "x". Coordinate descent.
inline void c_nnls(Eigen::MatrixXd& a, Eigen::VectorXd& b, Eigen::MatrixXd& h, const unsigned int sample) {
  double tol = 1;
  for (unsigned int it = 0; it < CD_MAXIT && (tol / b.size()) > CD_TOL; ++it) {
    tol = 0;
    for (unsigned int i = 0; i < h.rows(); ++i) {
      double diff = b(i) / a(i, i);
      if (-diff > h(i, sample)) {
        if (h(i, sample) != 0) {
          b -= a.col(i) * -h(i, sample);
          tol = 1;
          h(i, sample) = 0;
        }
      } else if (diff != 0) {
        h(i, sample) += diff;
        b -= a.col(i) * diff;
        tol += std::abs(diff / (h(i, sample) + TINY_NUM));
      }
    }
  }
}

// solves ax_i = b for a two-variable system of equations, where the "i"th column in "x" contains the solution.
// "denom" is a pre-conditioned denominator for solving by direct substition.
inline void nnls2(const Eigen::Matrix2d& a, const double b0, const double b1, const double denom, Eigen::MatrixXd& x, const unsigned int i, const bool nonneg) {
  // solve least squares
  if (nonneg) {
    const double a01b1 = a(0, 1) * b1;
    const double a11b0 = a(1, 1) * b0;
    if (a11b0 < a01b1) {
      x(0, i) = 0;
      x(1, i) = b1 / a(1, 1);
    } else {
      const double a01b0 = a(0, 1) * b0;
      const double a00b1 = a(0, 0) * b1;
      if (a00b1 < a01b0) {
        x(0, i) = b0 / a(0, 0);
        x(1, i) = 0;
      } else {
        x(0, i) = (a11b0 - a01b1) / denom;
        x(1, i) = (a00b1 - a01b0) / denom;
      }
    }
  } else {
    x(0, i) = (a(1, 1) * b0 - a(0, 1) * b1) / denom;
    x(1, i) = (a(0, 0) * b1 - a(0, 1) * b0) / denom;
  }
}

// solves ax = b for a two-variable system of equations, where "b" is given in the "i"th column of "w", and replaced with the solution "x".
// "denom" is a pre-conditioned denominator for solving by direct substitution.
inline void nnls2InPlace(const Eigen::Matrix2d& a, const double denom, Eigen::MatrixXd& w, const bool nonneg) {
  for (unsigned int i = 0; i < w.cols(); ++i) {
    if (nonneg) {
      const double a01b1 = a(0, 1) * w(1, i);
      const double a11b0 = a(1, 1) * w(0, i);
      if (a11b0 < a01b1) {
        w(0, i) = 0;
        w(1, i) /= a(1, 1);
      } else {
        const double a01b0 = a(0, 1) * w(0, i);
        const double a00b1 = a(0, 0) * w(1, i);
        if (a00b1 < a01b0) {
          w(0, i) /= a(0, 0);
          w(1, i) = 0;
        } else {
          w(0, i) = (a11b0 - a01b1) / denom;
          w(1, i) = (a00b1 - a01b0) / denom;
        }
      }
    } else {
      double b0 = w(0, i);
      w(0, i) = (a(1, 1) * b0 - a(0, 1) * w(1, i)) / denom;
      w(1, i) = (a(0, 0) * w(1, i) - a(0, 1) * b0) / denom;
    }
  }
}

void project(RcppML::SparseMatrix& A, const Eigen::MatrixXd& w, Eigen::MatrixXd& h, const bool nonneg, const double L1,
             const unsigned int threads, const bool mask_zeros, const std::vector<unsigned int>& samples, const std::vector<unsigned int>& features) {

  bool all_features = (features.size() == A.rows());
  if (w.rows() == 1) {
    h.setZero();
    double a = 0;
    for (unsigned int i = 0; i < w.cols(); ++i) a += w(0, i) * w(0, i);
    if (all_features) {
      for (unsigned int i = 0; i < h.cols(); ++i) {
        for (RcppML::SparseMatrix::InnerIterator it(A, samples[i]); it; ++it)
          h(0, i) += it.value() * w(0, it.row());
        h(0, i) /= a;
      }
    } else {
      for (unsigned int i = 0; i < h.cols(); ++i) {
        for (RcppML::SparseMatrix::InnerRangedIterator it(A, samples[i], features); it; ++it)
          h(0, i) += it.value() * w(0, it.rangedRow());
        h(0, i) /= a;
      }
    }
  } else if (w.rows() == 2) {
    Eigen::Matrix2d a = w * w.transpose();
    a.diagonal().array() += TINY_NUM;
    const double denom = a(0, 0) * a(1, 1) - a(0, 1) * a(0, 1);
    if (all_features) {
      for (unsigned int i = 0; i < h.cols(); ++i) {
        double b0 = 0, b1 = 0;
        for (RcppML::SparseMatrix::InnerIterator it(A, samples[i]); it; ++it) {
          const double val = it.value();
          const unsigned int r = it.row();
          b0 += val * w(0, r);
          b1 += val * w(1, r);
        }
        nnls2(a, b0, b1, denom, h, i, nonneg);
      }
    } else {
      for (unsigned int i = 0; i < h.cols(); ++i) {
        double b0 = 0, b1 = 0;
        for (RcppML::SparseMatrix::InnerRangedIterator it(A, samples[i], features); it; ++it) {
          const double val = it.value();
          const unsigned int r = it.rangedRow();
          b0 += val * w(0, r);
          b1 += val * w(1, r);
        }
        nnls2(a, b0, b1, denom, h, i, nonneg);
      }
    }
  } else {
    if (!mask_zeros) {
      Eigen::MatrixXd a = w * w.transpose();
      a.diagonal().array() += TINY_NUM;
      Eigen::LLT<Eigen::MatrixXd, 1> a_llt = a.llt();
      if(threads != 1){
        #ifdef _OPENMP
        #pragma omp parallel for num_threads(threads) schedule(dynamic)
        #endif
        for (unsigned int i = 0; i < h.cols(); ++i) {
          Eigen::VectorXd b = Eigen::VectorXd::Zero(h.rows());
          if (all_features) {
            for (RcppML::SparseMatrix::InnerIterator it(A, samples[i]); it; ++it)
              b += it.value() * w.col(it.row());
          } else {
            for (RcppML::SparseMatrix::InnerRangedIterator it(A, samples[i], features); it; ++it)
              b += it.value() * w.col(it.rangedRow());
          }
          if (L1 != 0) b.array() -= L1;

          h.col(i) = a_llt.solve(b);
          if (nonneg && (h.col(i).array() < 0).any()) {
            // if unconstrained solution contains negative values
            b -= a * h.col(i);
            // update h.col(sample) with NNLS solution by coordinate descent
            c_nnls(a, b, h, i);
          }
        }
      } else {
        // compile without OpenMP interference
        // see https://stackoverflow.com/questions/16807766/may-compiler-optimizations-be-inhibited-by-multi-threading
        for (unsigned int i = 0; i < h.cols(); ++i) {
          Eigen::VectorXd b = Eigen::VectorXd::Zero(h.rows());
          if (all_features) {
            for (RcppML::SparseMatrix::InnerIterator it(A, samples[i]); it; ++it)
              b += it.value() * w.col(it.row());
          } else {
            for (RcppML::SparseMatrix::InnerRangedIterator it(A, samples[i], features); it; ++it)
              b += it.value() * w.col(it.rangedRow());
          }
          if (L1 != 0) b.array() -= L1;

          h.col(i) = a_llt.solve(b);
          if (nonneg && (h.col(i).array() < 0).any()) {
            // if unconstrained solution contains negative values
            b -= a * h.col(i);
            // update h.col(sample) with NNLS solution by coordinate descent
            c_nnls(a, b, h, i);
          }
        }
      }
    } else {
      h.setZero();
      if(threads != 1){
        #ifdef _OPENMP
        #pragma omp parallel for num_threads(threads) schedule(dynamic)
        #endif
        for (unsigned int i = 0; i < h.cols(); ++i) {
          Eigen::VectorXi nnz = Rcpp::as<Eigen::VectorXi>(A.nonzeroRows(samples[i]));
          Eigen::MatrixXd w_ = submat(w, nnz);
          Eigen::MatrixXd a = w_ * w_.transpose();
          a.diagonal().array() += TINY_NUM;

          Eigen::VectorXd b = Eigen::VectorXd::Zero(h.rows());
          if (all_features) {
            for (RcppML::SparseMatrix::InnerIterator it(A, samples[i]); it; ++it)
              b += it.value() * w.col(it.row());
          } else {
            for (RcppML::SparseMatrix::InnerRangedIterator it(A, samples[i], features); it; ++it)
              b += it.value() * w.col(it.rangedRow());
          }
          if (L1 != 0) b.array() -= L1;

          if (!nonneg) h.col(i) = a.llt().solve(b);
          else c_nnls(a, b, h, i);
        }
      } else {
        // compile without OpenMP interference
        // see https://stackoverflow.com/questions/16807766/may-compiler-optimizations-be-inhibited-by-multi-threading
        for (unsigned int i = 0; i < h.cols(); ++i) {
          Eigen::VectorXi nnz = Rcpp::as<Eigen::VectorXi>(A.nonzeroRows(samples[i]));
          Eigen::MatrixXd w_ = submat(w, nnz);
          Eigen::MatrixXd a = w_ * w_.transpose();
          a.diagonal().array() += TINY_NUM;

          Eigen::VectorXd b = Eigen::VectorXd::Zero(h.rows());
          if (all_features) {
            for (RcppML::SparseMatrix::InnerIterator it(A, samples[i]); it; ++it)
              b += it.value() * w.col(it.row());
          } else {
            for (RcppML::SparseMatrix::InnerRangedIterator it(A, samples[i], features); it; ++it)
              b += it.value() * w.col(it.rangedRow());
          }
          if (L1 != 0) b.array() -= L1;

          if (!nonneg) h.col(i) = a.llt().solve(b);
          else c_nnls(a, b, h, i);
        }
      }
    }
  }
}

void project(const Eigen::MatrixXd& A, const Eigen::MatrixXd& w, Eigen::MatrixXd& h, const bool nonneg, const double L1,
             const unsigned int threads, const bool mask_zeros, const std::vector<unsigned int>& samples, const std::vector<unsigned int>& features) {

  bool all_features = (features.size() == (unsigned int)A.rows());
  if (w.rows() == 1) {
    h.setZero();
    double a = 0;
    for (unsigned int i = 0; i < w.cols(); ++i) a += w(0, i) * w(0, i);
    if (all_features) {
      for (unsigned int i = 0; i < h.cols(); ++i) {
        for (int j = 0; j < w.cols(); ++j)
          h(0, i) += A(j, samples[i]) * w(0, j);
        h(0, i) /= a;
      }
    } else {
      for (unsigned int i = 0; i < h.cols(); ++i) {
        for (int j = 0; j < w.cols(); ++j)
          h(0, i) += A(features[j], samples[i]) * w(0, j);
        h(0, i) /= a;
      }
    }
  } else if (w.rows() == 2) {
    Eigen::Matrix2d a = w * w.transpose();
    a.diagonal().array() += TINY_NUM;
    const double denom = a(0, 0) * a(1, 1) - a(0, 1) * a(0, 1);
    if (all_features) {
      for (unsigned int i = 0; i < h.cols(); ++i) {
        double b0 = 0, b1 = 0;
        for (int j = 0; j < w.cols(); ++j) {
          const double val = A(j, samples[i]);
          b0 += val * w(0, j);
          b1 += val * w(1, j);
        }
        nnls2(a, b0, b1, denom, h, i, nonneg);
      }
    } else {
      for (unsigned int i = 0; i < h.cols(); ++i) {
        double b0 = 0, b1 = 0;
        for (int j = 0; j < w.cols(); ++j) {
          const double val = A(features[j], samples[i]);
          b0 += val * w(0, j);
          b1 += val * w(1, j);
        }
        nnls2(a, b0, b1, denom, h, i, nonneg);
      }
    }
  } else {
    Eigen::MatrixXd a = w * w.transpose();
    a.diagonal().array() += TINY_NUM;
    Eigen::LLT<Eigen::MatrixXd, 1> a_llt = a.llt();
    if(threads == 1){
      #ifdef _OPENMP
      #pragma omp parallel for num_threads(threads) schedule(dynamic)
      #endif
      for (unsigned int i = 0; i < h.cols(); ++i) {
        Eigen::VectorXd b = Eigen::VectorXd::Zero(a.rows());
        if (all_features) {
          b += w * A.col(samples[i]);
        } else {
          for (unsigned int j = 0; j < w.cols(); ++j)
            b += A(features[j], samples[i]) * w.col(j);
        }
        if (L1 != 0) b.array() -= L1;

        h.col(i) = a_llt.solve(b);
        if (nonneg && (h.col(i).array() < 0).any()) {
          // if unconstrained solution contains negative values
          b -= a * h.col(i);
          // update h.col(sample) with NNLS solution by coordinate descent
          c_nnls(a, b, h, i);
        }
      }
    } else {
      // compile without OpenMP interference
      for (unsigned int i = 0; i < h.cols(); ++i) {
        Eigen::VectorXd b = Eigen::VectorXd::Zero(a.rows());
        if (all_features) {
          b += w * A.col(samples[i]);
        } else {
          for (unsigned int j = 0; j < w.cols(); ++j)
            b += A(features[j], samples[i]) * w.col(j);
        }
        if (L1 != 0) b.array() -= L1;

        h.col(i) = a_llt.solve(b);
        if (nonneg && (h.col(i).array() < 0).any()) {
          // if unconstrained solution contains negative values
          b -= a * h.col(i);
          // update h.col(sample) with NNLS solution by coordinate descent
          c_nnls(a, b, h, i);
        }
      }      
    }
  }
}

void project(RcppML::SparseMatrix& A, const Eigen::MatrixXd& w, Eigen::MatrixXd& h, const bool nonneg, const double L1,
             const unsigned int threads, const bool mask_zeros) {
  std::vector<unsigned int> samples(h.cols()), features(w.cols());
  std::iota(samples.begin(), samples.end(), 0);
  std::iota(features.begin(), features.end(), 0);
  project(A, w, h, nonneg, L1, threads, mask_zeros, samples, features);
}

void project(const Eigen::MatrixXd& A, const Eigen::MatrixXd& w, Eigen::MatrixXd& h, const bool nonneg, const double L1,
             const unsigned int threads, const bool mask_zeros) {
  std::vector<unsigned int> samples(h.cols()), features(w.cols());
  std::iota(samples.begin(), samples.end(), 0);
  std::iota(features.begin(), features.end(), 0);
  project(A, w, h, nonneg, L1, threads, mask_zeros, samples, features);
}

void projectInPlace(RcppML::SparseMatrix& A, const Eigen::MatrixXd& h, Eigen::MatrixXd& w, const bool nonneg, const double L1,
                    const unsigned int threads, const bool mask_zeros, const std::vector<unsigned int>& samples, const std::vector<unsigned int>& features) {

  bool all_features = (features.size() == A.rows());
  const unsigned int k = w.rows();
  w.setZero();
  if (k == 1) {
    double a = 0;
    for (unsigned int i = 0; i < h.cols(); ++i) a += h(0, i) * h(0, i);
    if (all_features) {
      for (unsigned int i = 0; i < h.cols(); ++i)
        for (RcppML::SparseMatrix::InnerIterator it(A, samples[i]); it; ++it)
          w(0, it.row()) += it.value() * h(0, i);
    } else {
      for (unsigned int i = 0; i < h.cols(); ++i)
        for (RcppML::SparseMatrix::InnerRangedIterator it(A, samples[i], features); it; ++it)
          w(0, it.rangedRow()) += it.value() * h(0, i);
    }
    for (unsigned int i = 0; i < w.cols(); ++i) w(0, i) /= a;
  } else if (k == 2) {
    Eigen::Matrix2d a = h * h.transpose();
    a.diagonal().array() += TINY_NUM;
    const double denom = a(0, 0) * a(1, 1) - a(0, 1) * a(0, 1);
    if (all_features) {
      for (unsigned int i = 0; i < h.cols(); ++i) {
        for (RcppML::SparseMatrix::InnerIterator it(A, samples[i]); it; ++it)
          for (unsigned int j = 0; j < 2; ++j)
            w(j, it.row()) += it.value() * h(j, i);
      }
    } else {
      for (unsigned int i = 0; i < h.cols(); ++i) {
        for (RcppML::SparseMatrix::InnerRangedIterator it(A, samples[i], features); it; ++it)
          for (unsigned int j = 0; j < 2; ++j)
            w(j, it.rangedRow()) += it.value() * h(j, i);
      }
    }
    nnls2InPlace(a, denom, w, nonneg);
  } else {
    // calculate "b", right-hand side of system of equations
    if(threads != 1){
      #ifdef _OPENMP
      #pragma omp parallel for num_threads(threads) schedule(dynamic)
      #endif
      for (unsigned int i = 0; i < h.cols(); ++i) {
        if (all_features) {
          for (RcppML::SparseMatrix::InnerIterator it(A, samples[i]); it; ++it)
            for (unsigned int j = 0; j < k; ++j)
              w(j, it.row()) += it.value() * h(j, i);
        } else {
          for (RcppML::SparseMatrix::InnerRangedIterator it(A, samples[i], features); it; ++it)
            for (unsigned int j = 0; j < k; ++j)
              w(j, it.rangedRow()) += it.value() * h(j, i);
        }
      }
    } else {
      // compile without OpenMP interference
      for (unsigned int i = 0; i < h.cols(); ++i) {
        if (all_features) {
          for (RcppML::SparseMatrix::InnerIterator it(A, samples[i]); it; ++it)
            for (unsigned int j = 0; j < k; ++j)
              w(j, it.row()) += it.value() * h(j, i);
        } else {
          for (RcppML::SparseMatrix::InnerRangedIterator it(A, samples[i], features); it; ++it)
            for (unsigned int j = 0; j < k; ++j)
              w(j, it.rangedRow()) += it.value() * h(j, i);
        }
      }
    }
    if (L1 != 0) w.array() -= L1;

    if (!mask_zeros) {
      // calculate "a", left-hand side of system of equations
      Eigen::MatrixXd a = h * h.transpose();
      a.diagonal().array() += TINY_NUM;
      Eigen::LLT<Eigen::MatrixXd, 1> a_llt = a.llt();

      // solve systems 
      if(threads != 1){
        #ifdef _OPENMP
        #pragma omp parallel for num_threads(threads) schedule(dynamic)
        #endif
        for (unsigned int i = 0; i < w.cols(); ++i) {
          Eigen::VectorXd b = w.col(i);
          w.col(i) = a_llt.solve(b);
          if (nonneg && (w.col(i).array() < 0).any()) {
            b -= a * w.col(i);
            c_nnls(a, b, w, i);
          }
        }
      } else {
        // compile without OpenMP interference
        for (unsigned int i = 0; i < w.cols(); ++i) {
          Eigen::VectorXd b = w.col(i);
          w.col(i) = a_llt.solve(b);
          if (nonneg && (w.col(i).array() < 0).any()) {
            b -= a * w.col(i);
            c_nnls(a, b, w, i);
          }
        }
      }
    } else {
      if(threads != 1){
        #ifdef _OPENMP
        #pragma omp parallel for num_threads(threads) schedule(dynamic)
        #endif
        for (int i = 0; i < w.cols(); ++i) {
          // calculate "a", left-hand side of system of equations

          // get indices in samples for which A.row(i) is non-zero
          std::vector<int> nnz_;
          for (unsigned int col = 0; col < h.cols(); ++col) {
            for (RcppML::SparseMatrix::InnerIterator it(A, samples[col]); it; ++it) {
              if (it.row() > i) continue;
              if (it.row() == i) {
                nnz_.push_back(col);
                continue;
              }
            }
          }
          Eigen::VectorXi nnz = Eigen::VectorXi::Map(nnz_.data(), nnz_.size());
          Eigen::MatrixXd h_ = submat(h, nnz);
          Eigen::MatrixXd a = h_ * h_.transpose();
          a.diagonal().array() += TINY_NUM;

          // solve systems
          Eigen::VectorXd b = w.col(i);
          if (!nonneg) w.col(i) = a.llt().solve(b);
          else c_nnls(a, b, w, i);
        }
      } else {
        // compile without OpenMP interference
        for (int i = 0; i < w.cols(); ++i) {
          // calculate "a", left-hand side of system of equations

          // get indices in samples for which A.row(i) is non-zero
          std::vector<int> nnz_;
          for (unsigned int col = 0; col < h.cols(); ++col) {
            for (RcppML::SparseMatrix::InnerIterator it(A, samples[col]); it; ++it) {
              if (it.row() > i) continue;
              if (it.row() == i) {
                nnz_.push_back(col);
                continue;
              }
            }
          }
          Eigen::VectorXi nnz = Eigen::VectorXi::Map(nnz_.data(), nnz_.size());
          Eigen::MatrixXd h_ = submat(h, nnz);
          Eigen::MatrixXd a = h_ * h_.transpose();
          a.diagonal().array() += TINY_NUM;

          // solve systems
          Eigen::VectorXd b = w.col(i);
          if (!nonneg) w.col(i) = a.llt().solve(b);
          else c_nnls(a, b, w, i);
        }
      }
    }
  }
}

void projectInPlace(const Eigen::MatrixXd& A, const Eigen::MatrixXd& h, Eigen::MatrixXd& w, const bool nonneg, const double L1,
                    const unsigned int threads, const bool mask_zeros, const std::vector<unsigned int>& samples, const std::vector<unsigned int>& features) {

  bool all_features = (features.size() == (unsigned int)A.rows());
  const unsigned int k = w.rows();
  w.setZero();
  if (k == 1) {
    double a = 0;
    for (unsigned int i = 0; i < h.cols(); ++i) a += h(0, i) * h(0, i);
    if (all_features) {
      for (unsigned int i = 0; i < h.cols(); ++i)
        for (int j = 0; j < w.cols(); ++j)
          w(0, j) += A(j, samples[i]) * h(0, i);
    } else {
      for (unsigned int i = 0; i < h.cols(); ++i)
        for (int j = 0; j < w.cols(); ++j)
          w(0, j) += A(features[j], samples[i]) * h(0, i);
    }
    for (unsigned int i = 0; i < w.cols(); ++i) w(0, i) /= a;
  } else if (k == 2) {
    Eigen::Matrix2d a = h * h.transpose();
    a.diagonal().array() += TINY_NUM;
    const double denom = a(0, 0) * a(1, 1) - a(0, 1) * a(0, 1);
    if (all_features) {
      for (unsigned int i = 0; i < h.cols(); ++i) {
        for (int j = 0; j < w.cols(); ++j)
          for (unsigned int l = 0; l < 2; ++l)
            w(l, j) += A(j, samples[i]) * h(l, i);
      }
    } else {
      for (unsigned int i = 0; i < h.cols(); ++i) {
        for (int j = 0; j < w.cols(); ++j)
          for (unsigned int l = 0; l < 2; ++l)
            w(l, j) += A(features[j], samples[i]) * h(l, i);
      }
    }
    nnls2InPlace(a, denom, w, nonneg);
  } else {
    Eigen::MatrixXd a = h * h.transpose();
    a.diagonal().array() += TINY_NUM;
    Eigen::LLT<Eigen::MatrixXd, 1> a_llt = a.llt();
    if(threads != 1){
      #ifdef _OPENMP
      #pragma omp parallel for num_threads(threads) schedule(dynamic)
      #endif
      for (unsigned int i = 0; i < h.cols(); ++i) {
        if (all_features) {
          for (int j = 0; j < w.cols(); ++j)
            for (unsigned int l = 0; l < k; ++l)
              w(l, j) += A(j, samples[i]) * h(l, i);
        } else {
          for (int j = 0; j < w.cols(); ++j)
            for (unsigned int l = 0; l < k; ++l)
              w(l, j) += A(features[j], samples[i]) * h(l, i);
        }
      }
    } else {
      // compile without OpenMP interference
      for (unsigned int i = 0; i < h.cols(); ++i) {
        if (all_features) {
          for (int j = 0; j < w.cols(); ++j)
            for (unsigned int l = 0; l < k; ++l)
              w(l, j) += A(j, samples[i]) * h(l, i);
        } else {
          for (int j = 0; j < w.cols(); ++j)
            for (unsigned int l = 0; l < k; ++l)
              w(l, j) += A(features[j], samples[i]) * h(l, i);
        }
      }
    }
    if (L1 != 0) w.array() -= L1;
    if(threads != 1){
      #ifdef _OPENMP
      #pragma omp parallel for num_threads(threads) schedule(dynamic)
      #endif
      for (unsigned int i = 0; i < w.cols(); ++i) {
        Eigen::VectorXd b = w.col(i);
        w.col(i) = a_llt.solve(b);
        if (nonneg && (w.col(i).array() < 0).any()) {
          // if unconstrained solution contains negative values
          b -= a * w.col(i);
          // update h.col(sample) with NNLS solution by coordinate descent
          c_nnls(a, b, w, i);
        }
      }
    } else {
      // compile without OpenMP interference
      for (unsigned int i = 0; i < w.cols(); ++i) {
        Eigen::VectorXd b = w.col(i);
        w.col(i) = a_llt.solve(b);
        if (nonneg && (w.col(i).array() < 0).any()) {
          // if unconstrained solution contains negative values
          b -= a * w.col(i);
          // update h.col(sample) with NNLS solution by coordinate descent
          c_nnls(a, b, w, i);
        }
      }
    }
  }
}

#endif