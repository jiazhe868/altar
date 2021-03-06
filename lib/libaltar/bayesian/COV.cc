// -*- C++ -*-
//
// michael a.g. aïvázis
// california institute of technology
// (c) 2010-2013 all rights reserved
//


// for the build system
#include <portinfo>

// for debugging
# include <cassert>

// externals
#include <cmath>
#include <iostream>
#include <iomanip>
#include <gsl/gsl_sys.h>
#include <gsl/gsl_min.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_roots.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_histogram.h>
#include <gsl/gsl_statistics.h>
#include <gsl/gsl_sort_vector.h>

#include <pyre/journal.h>

// get my declarations
#include "COV.h"
// and my dependencies
#include "CoolingStep.h"

// workhorses
namespace cov {
    // the COV calculator
    static double cov(double, void *);
    // and its parameter structure
    struct args {
        double dbeta; // the current proposal for dbeta
        double cov; // the current value of COV
        double metric; // the latest value of our objective function

        // inputs from the higher levels
        gsl_vector * w; // the vector of weights
        gsl_vector * llk; // the vector of data log-likelihoods
        double llkMedian; // the median value of the log-likelihoods
        double target; // the COV value we are aiming for; should be 1
    };
}

// interface
void
altar::bayesian::COV::
update(state_t & state)
{
    // get the problem size
    const size_t samples = state.samples();
    // grab the data likelihood vector
    vector_t * dataLLK = state.data();
    // make a vector for the weights
    vector_t * w = gsl_vector_alloc(samples);

    // first, let's use it to find the median sample
    gsl_vector_memcpy(w, dataLLK);
    // sort it
    gsl_sort_vector(w);
    // find the median
    double median = gsl_stats_median_from_sorted_data(w->data, w->stride, w->size);

    // ok, now zero out w
    gsl_vector_set_zero(w);
    // compute the temperature increment
    dbeta(dataLLK, median, w);
    // save the new temperature
    state.beta(_beta);

    // compute the covariance
    computeCovariance(state, w);

    // rank and reorder the samples according to their likelihoods
    rankAndShuffle(state, w);

    // free the temporaries
    gsl_vector_free(w);

    // all done
    return;
}


double
altar::bayesian::COV::
dbeta(vector_t *llk, double llkMedian, vector_t *w)
{
    // build my debugging channel
    pyre::journal::debug_t debug("altar.beta");

    // consistency checks
    assert(_betaMin == 0);
    assert(_betaMax == 1);

    // the beta search region
    double beta_low = 0;
    double beta_high = _betaMax - _beta;
    double beta_guess = _betaMin + 5.0e-5;

    assert(beta_high >= beta_low);
    assert(beta_high >= beta_guess);
    assert(beta_guess >= beta_low);

    // allocate space for the parameters
    cov::args covargs;
    // attach the two vectors
    covargs.w = w;
    covargs.llk = llk;
    // store our initial guess
    covargs.dbeta = beta_guess;
    // initialize the COV target
    covargs.target = _target;
    // initialize the median of the log-likelihoods
    covargs.llkMedian = llkMedian;

    // search bounds and initial guess
    double f_beta_high = cov::cov(beta_high, &covargs);

    // check whether we can skip straight to beta = 1
    if (covargs.cov < _target || std::abs(covargs.cov-_target) < _tolerance) {
        debug
            << pyre::journal::at(__HERE__)
            << " ** skipping to beta = " << _betaMax << " **"
            << pyre::journal::endl;
        // save my state
        _beta = _betaMax;
        _cov = covargs.cov;
        // all done
        return beta_high;
    }

    double f_beta_low = cov::cov(beta_low, &covargs);
    // do this last so our first printout reflects the values at out guess
    double f_beta_guess = cov::cov(beta_guess, &covargs);

    // lie to the minimizer, if necessary
    // if (f_beta_low < f_beta_guess) f_beta_low = 1.01 * f_beta_guess;
    // if (f_beta_high < f_beta_guess) f_beta_high = 1.01 * f_beta_guess;

    // instantiate the minimizer
    gsl_min_fminimizer * minimizer = gsl_min_fminimizer_alloc(gsl_min_fminimizer_brent);
    // set up the minimizer call back
    gsl_function F;
    F.function = cov::cov;
    F.params = &covargs;
    // prime it
    gsl_min_fminimizer_set_with_values(
                                       minimizer, &F,
                                       beta_guess, f_beta_guess,
                                       beta_low, f_beta_low,
                                       beta_high, f_beta_high);
    // duplicate the catmip output, for now
    size_t iter = 0;
    if (debug) {
        std::cout
            << "    "
            << "Calculating dbeta using "
            << gsl_min_fminimizer_name(minimizer) << " method" << std::endl
            << "      median data llk: "
            << std::fixed << std::setw(11) << std::setprecision(4) << llkMedian << std::endl
            << "      target: " << _target << std::endl
            << "      tolerance: " << _tolerance << std::endl
            << "      max iterations: " << _maxIterations
            << std::endl;
        std::cout
            << "     "
            << std::setw(6) << " iter"
            " [" << std::setw(11) << "lower"
            << ", " << std::setw(11) << "upper" << "]"
            << " " << std::setw(11) << "dbeta  "
            << " " << std::setw(11) << "cov  "
            << " " << std::setw(11) << "err  "
            << " " << std::setw(11) << "f(dbeta)"
            << std::endl;
        std::cout.setf(std::ios::scientific, std::ios::floatfield);
        std::cout
            << "     "
            << std::setw(5) << iter
            << " [" << std::setw(11) << std::setprecision(4) << beta_low
            << ", " << std::setw(11) << beta_high << "] "
            << " " << std::setw(11) << covargs.dbeta
            << " " << std::setw(11) << covargs.cov
            << " " << std::setw(11) << covargs.cov - _target
            << " " << std::setw(11) << covargs.metric
            << std::endl;
    }

    // the GSL flag
    int status;
    // iterate, looking for the minimum
    do {
        iter++;
        status = gsl_min_fminimizer_iterate(minimizer);
        beta_low = gsl_min_fminimizer_x_lower(minimizer);
        beta_high = gsl_min_fminimizer_x_upper(minimizer);
        status = gsl_root_test_residual(covargs.cov-_target,  _tolerance);

        // print the result
        if (debug) {
            std::cout
                << "     "
                << std::setw(5) << iter
                << " [" << std::setw(11) << std::setprecision(4) << beta_low
                << ", " << std::setw(11) << beta_high << "] "
                << " " << std::setw(11) << covargs.dbeta
                << " " << std::setw(11) << covargs.cov
                << " " << std::setw(11) << covargs.cov - _target
                << " " << std::setw(11) << covargs.metric;

            if (status == GSL_SUCCESS) {
                std::cout << " (Converged)";
            }
            std::cout << std::endl;
        }

    } while (status == GSL_CONTINUE && iter < _maxIterations);

    // get the best guess at the minimum
    double dbeta = gsl_min_fminimizer_x_minimum(minimizer);
    // make sure that we are left with the COV and weights evaluated for this guess
    cov::cov(dbeta, &covargs);

    // free the minimizer
    gsl_min_fminimizer_free(minimizer);

    // adjust my state
    _cov = covargs.cov;
    _beta += dbeta;
    // return the beta update
    return dbeta;
}


// meta-methods
altar::bayesian::COV::
~COV()
{}


// implementation details
void
altar::bayesian::COV::
computeCovariance(state_t & state, vector_t * weights) const
{
    // unpack the problem sizes
    const size_t samples = state.samples();
    const size_t parameters = state.parameters();

    // get the sample matrix
    matrix_t * theta = state.theta();
    // and the covariance
    matrix_t * sigma = state.sigma();

    // zero it out before we start accumulating values there
    gsl_matrix_set_zero(sigma);

    // build a vector for the weighted mean of each parameter
    vector_t * thetaBar = gsl_vector_alloc(parameters);
    // for each parameter
    for (size_t parameter=0; parameter<parameters; ++parameter) {
        // get column of theta that has the value of this parameter across all samples
        gsl_vector_view column = gsl_matrix_column(theta, parameter);
        // and treat it like a vector
        vector_t * values = &column.vector;
        // compute the mean
        double mean = gsl_stats_wmean(
                                      weights->data, weights->stride,
                                      values->data, values->stride, values->size
                                      );
        // set the corresponding value of theta bar
        gsl_vector_set(thetaBar, parameter, mean);
    }

    // start filling out sigma
    for (size_t sample=0; sample<samples; ++sample) {
        // get the row with the sample
        gsl_vector_view row = gsl_matrix_row(theta, sample);
        // form {sigma += w[i] sample sample^T}
        gsl_blas_dsyr(CblasLower, gsl_vector_get(weights, sample), &row.vector, sigma);
    }
    // subtract {thetaBar . thetaBar^T}
    gsl_blas_dsyr(CblasLower, -1, thetaBar, sigma);

    // fill the upper triangle
    for (size_t i=0; i<parameters; ++i) {
        for (size_t j=0; j<1; ++j) {
            gsl_matrix_set(sigma, j,i, gsl_matrix_get(sigma, i,j));
        }
    }

    // condition the covariance matrix
    conditionCovariance(sigma);

    // free the temporaries
    gsl_vector_free(thetaBar);

    // all done
    return;
}

void
altar::bayesian::COV::
conditionCovariance(matrix_t * sigma) const
{
    // all done
    return;
}

void
altar::bayesian::COV::
rankAndShuffle(state_t & state, vector_t * weights) const
{
    // unpack the problem size
    const size_t samples = state.samples();
    const size_t parameters = state.parameters();

    // allocate storage for the histogram bins
    double * ticks = new double[samples+1];
    // initialize the first tick
    double tick = ticks[0] = 0;
    // use the weights to build the bins for the histogram
    for (size_t sample=0; sample<samples; ++sample) {
        // accumulate
        tick += gsl_vector_get(weights, sample);
        // store
        ticks[sample+1] = tick;
    }

    // allocate a histogram
    gsl_histogram * h = gsl_histogram_alloc(samples);
    // set the bins
    gsl_histogram_set_ranges(h, ticks, samples+1);
    // fill the histogram
    for (size_t sample=0; sample<samples; ++sample) {
        // with random number in the range (0,1)
        gsl_histogram_increment(h, gsl_ran_flat(_rng, 0, 1));
    }

    // a vector for the histogram counts
    vector_t * counts = gsl_vector_alloc(samples);
    // fill it with the histogram information
    for (size_t sample=0; sample<samples; ++sample) {
        gsl_vector_set(counts, sample, gsl_histogram_get(h, sample));
    }
    // allocate a permutation
    gsl_permutation * p = gsl_permutation_alloc(samples);
    // sort the counts
    gsl_sort_vector_index(p, counts);
    // in reverse order
    gsl_permutation_reverse(p);

    // get the state vectors
    matrix_t * theta = state.theta();
    vector_t * prior = state.prior();
    vector_t * data = state.data();
    vector_t * posterior = state.posterior();

    // allocate duplicates
    matrix_t * thetaOld = gsl_matrix_alloc(samples, parameters);
    vector_t * priorOld = gsl_vector_alloc(samples);
    vector_t * dataOld = gsl_vector_alloc(samples);
    vector_t * posteriorOld = gsl_vector_alloc(samples);
    // make copies of all these
    gsl_matrix_memcpy(thetaOld, theta);
    gsl_vector_memcpy(priorOld, prior);
    gsl_vector_memcpy(dataOld, data);
    gsl_vector_memcpy(posteriorOld, posterior);

    // the number of samples we have processed
    size_t done = 0;
    // start shuffling the samples and likelihoods around
    for (size_t sample=0; sample<samples; ++sample) {
        // the index of this sample in the original set
        size_t oldIndex = gsl_permutation_get(p, sample);
        // and its multiplicity
        size_t count = static_cast<size_t>(gsl_vector_get(counts, oldIndex));
        // if the count has dropped down to zero
        if (count == 0) {
            // get out of here
            break;
        }

        // get the row from the original samples set
        gsl_vector_view row = gsl_matrix_row(thetaOld, oldIndex);
        // otherwise, duplicate this sample {count} number of times
        for (size_t dupl=0; dupl<count; ++dupl) {
            // by setting {count} consecutive rows of theta to this sample
            gsl_matrix_set_row(theta, done, &row.vector);
            // and similarly for the log likelihoods
            gsl_vector_set(prior, done, gsl_vector_get(priorOld, oldIndex));
            gsl_vector_set(data, done, gsl_vector_get(dataOld, oldIndex));
            gsl_vector_set(posterior, done, gsl_vector_get(posteriorOld, oldIndex));
            // update the {done} count
            done += 1;
        }
    }

    // free the temporaries
    delete [] ticks;
    gsl_histogram_free(h);
    gsl_vector_free(counts);
    gsl_permutation_free(p);

    gsl_matrix_free(thetaOld);
    gsl_vector_free(priorOld);
    gsl_vector_free(dataOld);
    gsl_vector_free(posteriorOld);

    // all done
    return;
}



double cov::cov(double dbeta, void * parameters)
{
    // get my auxiliary parameters
    args & p = *static_cast<cov::args *>(parameters);
    // store dbeta
    p.dbeta = dbeta;

    // std::cout << " ** altar.beta: dbeta = " << dbeta << std::endl;

    // initialize {w}
    for (size_t i = 0; i < p.w->size; i++) {
        gsl_vector_set(p.w, i, std::exp(dbeta * (gsl_vector_get(p.llk, i)  - p.llkMedian)));
    }
    // normalize
    double wsum = p.w->size * gsl_stats_mean(p.w->data, p.w->stride, p.w->size);
    gsl_vector_scale(p.w, 1/wsum);

    // compute the COV
    double mean = gsl_stats_mean(p.w->data, p.w->stride, p.w->size);
    double sdev = gsl_stats_sd(p.w->data, p.w->stride, p.w->size);
    double cov = sdev / mean;
    // store it
    p.cov = cov;

    // if the COV is not well-defined
    if (gsl_isinf(cov) || gsl_isnan(cov)) {
        // set our metric to some big value
        p.metric = 1e100;
    } else {
        // otherwise
        p.metric = gsl_pow_2(cov - p.target);
    }

    // and return
    return p.metric;
}

// end of file
