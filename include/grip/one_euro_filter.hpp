#pragma once

#include <cmath>

namespace grip
{

/**
 * Exponential smoothing with a fixed smoothing factor.
 */
class LowPassFilter
{
public:
    LowPassFilter() = default;

    double filter(double x, double a)
    {
        if (!initialized_)
        {
            s_ = x;
            initialized_ = true;
        }
        else
        {
            s_ = a * x + (1.0 - a) * s_;
        }
        return s_;
    }

    bool initialized() const noexcept { return initialized_; }
    double value() const noexcept { return s_; }
    void reset() noexcept
    {
        initialized_ = false;
        s_ = 0.0;
    }

private:
    bool initialized_ = false;
    double s_ = 0.0;
};

/**
 * One Euro Filter (Casiez, Roussel & Vogel — CHI 2012).
 *
 * A low-pass filter whose cutoff frequency adapts to the signal speed:
 * slow motion is smoothed heavily (little jitter), fast motion raises the
 * cutoff so the output stays responsive.
 *
 *   cutoff = min_cutoff + beta * |dx/dt filtered|
 *
 * Parameters
 *  - freq        sampling rate used when no timestamp is supplied (Hz)
 *  - min_cutoff  cutoff at zero speed (Hz); lower = smoother but more lag
 *  - beta        speed coefficient; higher = less lag during fast motion
 *  - d_cutoff    cutoff for the derivative estimate (Hz)
 *
 * `filter()` accepts an optional timestamp in **seconds**; when supplied the
 * real sample interval is used, which keeps the filter stable even if the
 * receive rate fluctuates.
 */
class OneEuroFilter
{
public:
    OneEuroFilter(double freq = 100.0,
                  double min_cutoff = 1.0,
                  double beta = 0.05,
                  double d_cutoff = 1.0)
        : freq_(freq), min_cutoff_(min_cutoff), beta_(beta), d_cutoff_(d_cutoff) {}

    /** Filter a sample. `timestamp` is in seconds; < 0 uses 1/freq. */
    double filter(double x, double timestamp = -1.0)
    {
        double dt = -1.0;
        if (timestamp >= 0.0)
        {
            if (last_ts_ >= 0.0)
                dt = timestamp - last_ts_;
            last_ts_ = timestamp;
        }
        // Reject invalid, stalled or gapped intervals and fall back to nominal.
        if (!(dt > 0.0) || dt > 1.0)
            dt = 1.0 / freq_;
        return filter_with_dt(x, dt);
    }

    /** Filter a sample using an explicit sample interval (seconds). */
    double filter_with_dt(double x, double dt)
    {
        if (!(dt > 0.0))
            dt = 1.0 / freq_;

        const double dx = prev_initialized_ ? (x - x_prev_) / dt : 0.0;
        const double edx = d_filter_.filter(dx, alpha(d_cutoff_, dt));
        const double cutoff = min_cutoff_ + beta_ * std::fabs(edx);
        const double y = x_filter_.filter(x, alpha(cutoff, dt));

        x_prev_ = x;
        prev_initialized_ = true;
        return y;
    }

    void reset() noexcept
    {
        x_filter_.reset();
        d_filter_.reset();
        x_prev_ = 0.0;
        prev_initialized_ = false;
        last_ts_ = -1.0;
    }

    void set_params(double freq, double min_cutoff, double beta, double d_cutoff)
    {
        freq_ = freq > 0.0 ? freq : freq_;
        min_cutoff_ = min_cutoff;
        beta_ = beta;
        d_cutoff_ = d_cutoff;
    }

    double freq() const noexcept { return freq_; }
    double min_cutoff() const noexcept { return min_cutoff_; }
    double beta() const noexcept { return beta_; }
    double d_cutoff() const noexcept { return d_cutoff_; }

private:
    static double alpha(double cutoff, double dt)
    {
        if (cutoff <= 0.0)
            return 1.0;
        const double tau = 1.0 / (2.0 * M_PI * cutoff);
        return 1.0 / (1.0 + tau / dt);
    }

    double freq_ = 100.0;
    double min_cutoff_ = 1.0;
    double beta_ = 0.05;
    double d_cutoff_ = 1.0;

    LowPassFilter x_filter_;
    LowPassFilter d_filter_;
    double x_prev_ = 0.0;
    bool prev_initialized_ = false;
    double last_ts_ = -1.0;
};

} // namespace grip
