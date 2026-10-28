#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <SDL3/SDL.h>
#include "vulkan_context.hpp"

#include <chrono>
#include <thread>
#ifndef _WIN32
#include <bits/ios_base.h>
#endif

#include <fmt/format.h>

/**
 * Clock
 */

using TimePoint = std::chrono::high_resolution_clock::time_point;
using Duration = std::chrono::high_resolution_clock::duration;

class Clock
{
  TimePoint currentTimeSec;
  TimePoint lastTimeSec;
  Duration deltaSec; /* Delta Time */

public:
  void update()
  {
    lastTimeSec = currentTimeSec;
    currentTimeSec = std::chrono::high_resolution_clock::now();
    deltaSec = currentTimeSec - lastTimeSec;
  }
};

/**
 * Frame rate limiter.
 *
 * Target rate sweeps between min and max so VRR/G-SYNC can be observed.
 * Max defaults to the display refresh rate. limit() holds each frame so the
 * realized rate does not exceed that target; time already spent rendering is
 * subtracted from the budget.
 */
class FpsController
{
  using Clock = std::chrono::steady_clock;

  double frameRateFloor;
  double frameRateMin;
  double frameRateMax;
  double refreshRateHz;

  double currentSimulatedFrameRate;
  double elapsedSec;

  Clock::time_point epoch;
  Clock::time_point frameStart;
  Clock::duration frameDelta;
  Clock::duration targetFrameDuration;
  bool started;

  static Clock::duration durationForRate(double fps)
  {
    const auto exact = std::chrono::duration<double>(1.0 / fps);
    auto ticks = std::chrono::duration_cast<Clock::duration>(exact);
    /* Ceil so the cap is never shorter than 1/fps. */
    if (std::chrono::duration<double>(ticks) < exact)
      ++ticks;
    return ticks;
  }

  void updateTarget(Clock::time_point now)
  {
    elapsedSec = std::chrono::duration<double>(now - epoch).count();

    const double clampedMin = std::max(frameRateFloor, std::min(frameRateMin, frameRateMax));
    const double clampedMax = std::max(clampedMin, frameRateMax);
    const double range = clampedMax - clampedMin;
    const double mean = (clampedMin + clampedMax) / 2.0;
    const double amplitude = range / 2.0;

    currentSimulatedFrameRate = mean + amplitude * std::sin(elapsedSec);
    if (currentSimulatedFrameRate < frameRateFloor)
      currentSimulatedFrameRate = frameRateFloor;

    targetFrameDuration = durationForRate(currentSimulatedFrameRate);
  }

public:
  explicit FpsController(double refreshRate)
    : frameRateFloor(10.0)
    , refreshRateHz(refreshRate > 1.0 ? refreshRate : 60.0)
    , currentSimulatedFrameRate(0.0)
    , elapsedSec(0.0)
    , epoch(Clock::now())
    , frameStart(epoch)
    , frameDelta(Clock::duration::zero())
    , started(false)
  {
    frameRateMax = refreshRateHz;
    frameRateMin = std::min(30.0, frameRateMax);
    currentSimulatedFrameRate = frameRateMax;
    targetFrameDuration = durationForRate(currentSimulatedFrameRate);
  }

  void increaseMin(int byNrOfFrames)
  {
    frameRateMin = std::min(frameRateMin + byNrOfFrames, frameRateMax);
  }

  void decreaseMin(int byNrOfFrames)
  {
    frameRateMin = std::max(frameRateMin - byNrOfFrames, frameRateFloor);
  }

  void increaseMax(int byNrOfFrames)
  {
    frameRateMax += byNrOfFrames;
  }

  void decreaseMax(int byNrOfFrames)
  {
    frameRateMax = std::max(frameRateMax - byNrOfFrames, frameRateMin);
  }

  void beginFrame()
  {
    const auto now = Clock::now();
    if (!started) {
      epoch = now;
      frameStart = now;
      started = true;
    }

    frameDelta = now - frameStart;
    frameStart = now;
    updateTarget(now);
  }

  /* Block until this frame has lasted 1/targetRate. No-op if already late. */
  void limit() const
  {
    const auto deadline = frameStart + targetFrameDuration;
    const auto now = Clock::now();
    if (now >= deadline)
      return;

    /* sleep_until can wake ~1ms late. Sleep the bulk, then spin the tail so
       the cap stays accurate above 60 Hz. */
    constexpr auto spinSlack = std::chrono::microseconds(1000);
    if (now + spinSlack < deadline)
      std::this_thread::sleep_until(deadline - spinSlack);

    while (Clock::now() < deadline) {
    }
  }

  double minFrameRate() const
  {
    return frameRateMin;
  }

  double maxFrameRate() const
  {
    return frameRateMax;
  }

  double refreshRate() const
  {
    return refreshRateHz;
  }

  double targetFrameRate() const
  {
    return currentSimulatedFrameRate;
  }

  double elapsedSeconds() const
  {
    return elapsedSec;
  }

  double measuredFrameRate() const
  {
    const double dt = std::chrono::duration<double>(frameDelta).count();
    if (dt <= 0.0)
      return 0.0;
    return 1.0 / dt;
  }
};

/**
* Application
*/

class MyAppl
{
  int rectangleSpeed;
  bool running;

  SDL_Window* window;
  VulkanContext vkCtx;
  FpsController fps;

  int statsFrames;
  std::chrono::steady_clock::time_point statsStart;

public:
  MyAppl(SDL_Window* Window, double refreshRateHz)
    : rectangleSpeed(5)
    , running(false)
    , window(Window)
    , fps(refreshRateHz)
    , statsFrames(0)
    , statsStart(std::chrono::steady_clock::now())
  {
    vkCtx = VulkanContext(window, 1920, 1200);
  }

  void init()
  {
    SDL_ShowWindow(window);
    running = true;
  }

  void run()
  {
    while (running) {
      beginFrame();
      processEvents();

      /* Constant world speed; pacing comes from the limiter, not the clock. */
      const double travelSec = rectangleSpeed;
      const float x = static_cast<float>(std::fmod(fps.elapsedSeconds(), travelSec) / travelSec) * 2.0f;
      vkCtx.Update(x);
      vkCtx.Draw();
      endFrame();
    }

    cleanup();
  }

  void beginFrame()
  {
    fps.beginFrame();
  }

  void processEvents()
  {
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      switch (event.type)
      {
      case SDL_EVENT_QUIT:
        running = false;
        break;
      case SDL_EVENT_KEY_UP:
        switch (event.key.scancode)
        {
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_Q:
          running = false;
          fmt::print("Exit app!\n");
          break;
        case SDL_SCANCODE_UP:
          fps.increaseMax(10);
          break;
        case SDL_SCANCODE_DOWN:
          fps.decreaseMax(10);
          break;
        case SDL_SCANCODE_PAGEUP:
          fps.increaseMin(10);
          break;
        case SDL_SCANCODE_PAGEDOWN:
          fps.decreaseMin(10);
          break;
        default:
          break;
        }
        break;
      default:
        break;
      }
    }
  }

  void endFrame()
  {
    fps.limit();

    ++statsFrames;
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - statsStart).count();
    if (elapsed >= 1.0) {
      fmt::print(
        "fps {:.1f}  target {:.1f}  range {:.0f}-{:.0f}  refresh {:.3f}\n",
        statsFrames / elapsed,
        fps.targetFrameRate(),
        fps.minFrameRate(),
        fps.maxFrameRate(),
        fps.refreshRate());
      statsFrames = 0;
      statsStart = now;
    }
  }

  void cleanup()
  {
    SDL_DestroyWindow(window);
    SDL_Quit();
  }
};

typedef struct FrameContext_t
{
  double frameDelay;
  int animationDurationSec;
} FrameContext;

double queryRefreshRateHz(SDL_Window* window)
{
  SDL_DisplayID displayId = SDL_GetDisplayForWindow(window);
  if (displayId == 0)
    displayId = SDL_GetPrimaryDisplay();

  const SDL_DisplayMode* mode = nullptr;
  if (displayId != 0) {
    mode = SDL_GetCurrentDisplayMode(displayId);
    if (mode == nullptr)
      mode = SDL_GetDesktopDisplayMode(displayId);
  }

  if (mode != nullptr && mode->refresh_rate_numerator > 0 && mode->refresh_rate_denominator > 0)
    return static_cast<double>(mode->refresh_rate_numerator) / mode->refresh_rate_denominator;

  if (mode != nullptr && mode->refresh_rate > 0.0f)
    return mode->refresh_rate;

  return 0.0;
}

int main(int, char**)
{
  /* Application initialization */
  SDL_Init(SDL_INIT_VIDEO);

  uint32_t windowFlags = SDL_WINDOW_VULKAN /*| SDL_WINDOW_FULLSCREEN */;
  SDL_Window* window = SDL_CreateWindow(
    APP_NAME,
    1920,
    1200,
    windowFlags);
  if (window == nullptr)
  {
    printf("Failed to create SDL_Window. Exiting app.\n");
    return -1;
  }

  const double refreshRateHz = queryRefreshRateHz(window);
  if (refreshRateHz > 1.0)
    fmt::print("Display refresh rate {:.3f} Hz (default max fps)\n", refreshRateHz);
  else
    fmt::print("Could not query refresh rate, default max fps 60\n");

  MyAppl app(window, refreshRateHz);

  app.init();
  app.run();

  return 0;
}
