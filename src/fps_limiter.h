#ifndef FPS_LIMITER_H
#define FPS_LIMITER_H

namespace fallout {

class FpsLimiter {
public:
    FpsLimiter(unsigned int fps = 60);
    void mark();
    void throttle() const;

private:
    const unsigned int _fps;
    unsigned long long _ticks;
    mutable double _deadline = 0;
    mutable unsigned int _lastFps = 0;
};

} // namespace fallout

#endif /* FPS_LIMITER_H */
