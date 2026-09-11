#include <cmath>

#include <SDL3/SDL.h>
#include "vulkan_context.hpp"

#include <chrono>
#include <thread>
#include <bits/ios_base.h>

#include <fmt/core.h>

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
 * FrameRateController
 */

class FpsController
{
  double frameRateFloor;
  double frameRateMin;
  double frameRateMax;

  double currentSimulatedFrameRate;
  double nextFrameDelaySec;

public:
  FpsController(int refreshRate)
  {
    frameRateFloor = 10;
    frameRateMin = 30;
    frameRateMax = std::max(60, refreshRate);
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

  void computeNextFrameDelayMsec(
    double currentTimeSec)
  {
    const double frameRateMin = std::max(frameRateFloor, frameRateMin);
    const double frameRateRange = frameRateMax - frameRateMin;
    const double frameRateRangeMean = (frameRateMin + frameRateMax) / 2.0;
    const double frameRateAmplitude = frameRateRange / 2.0;

    currentSimulatedFrameRate = frameRateRangeMean + frameRateAmplitude * sin(currentTimeSec);

    nextFrameDelaySec = 1.0 / currentSimulatedFrameRate;
  }
};


/**
* Application
*/

class MyAppl
{
  int rectangleSpeed;
  bool running;

  TimePoint startFrame;
  uint64_t targetFrameTimeNs;

  SDL_Window* window;
  VulkanContext vkCtx;


public:
  MyAppl(SDL_Window* Window)
    : running(false), window(Window), rectangleSpeed(5)
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

      vkCtx.Update(0.5f);
      vkCtx.Draw();
      endFrame();
    }

    cleanup();
  }

  void beginFrame()
  {
    startFrame = std::chrono::high_resolution_clock::now();
    updateClock(&app->clock);
    computeNextFrameDelayMsec(&app->frameRateController, app->clock.currentTimeSec);

    frameContext->frameDelay = app->frameRateController.nextFrameDelaySec;
    frameContext->animationDurationSec = app->animationDurationSec;
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
          {
            running = false;
            printf("Exit app!\n");
            break;
          }
        case SDL_SCANCODE_PAGEUP:
          break;
        case SDL_SCANCODE_PAGEDOWN:
        default:
          break;
        }
      default:
        break;
      }
    }
  }

  void endFrame()
  {
    auto endFrame = std::chrono::high_resolution_clock::now() - startFrame;
    // timespec ts{};
    // ts.tv_sec = 0;
    // ts.tv_nsec = frameContext->frameDelay * 1000000000L;

    std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
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

float calcBarPosition()
{
  static float translation = 0.0;

  // We are in NDC space total width is 2 (-1 to 1)
  const float speedPixelPerSec = 2.0f / animationDurationSec;
  translation += speedPixelPerSec * clock.deltaSec;

  if (translation >= 2.0f)
    translation = 0.0f;

  return translation;
}

#ifdef USE_OPENGL
int printText(const char* format, ...)
{
  char buffer[1024];
  va_list arg;
  int ret;
  GLint pos[4];

  /* Format the text */
  va_start(arg, format);
  ret = vsnprintf(buffer, sizeof(buffer), format, arg);
  va_end(arg);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();

  /* Set raster position to force usage of last specified color */
  glGetIntegerv(GL_CURRENT_RASTER_POSITION, pos);
  glLoadIdentity();
  glRasterPos2iv(pos);

  /* Print it */
  glutBitmapString(GLUT_BITMAP_HELVETICA_18, (const unsigned char*)buffer);

  glPopMatrix();

  return ret;
}

void printStatus(const char* label, bool available, bool enabled)
{
  printText(label);

  glPushAttrib(GL_CURRENT_BIT);

  if (available)
  {
    glColor3f(1.0, 1.0, 0.0);
    printText("%s", enabled ? "ON" : "OFF");
  }
  else
  {
    glColor3f(1.0, 0.0, 0.0);
    printText("N/A");
  }

  glPopAttrib();

  /* Return to the begining of line */
  int width = glutBitmapLength(GLUT_BITMAP_HELVETICA_18, (const unsigned char*)label);
  int height = glutBitmapHeight(GLUT_BITMAP_HELVETICA_18);
  glBitmap(0, 0, 0, 0, -width, -height, NULL);
}

void drawScene()
{
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glColor3f(0.9f, 0.9f, 0.9f);

  /* Draw a vertical bar of 5% screen width */
  glTranslatef(computeVerticalBarXPosition(), 0.0f, 0.0f);
  glRecti(0, 0, glutGet(GLUT_WINDOW_WIDTH) * 0.05, glutGet(GLUT_WINDOW_HEIGHT));
}

void drawGauge(int heightPx, int gradMin, int gradMax, int gradStep, double currentValue)
{
  const int width = 15; /* px */
  const int gradLabelYOffset = -6; /* px */

  if (gradMax == gradMin)
  {
    gradMin = gradMax - 10;
    gradMax = gradMax + 10;
  }

  const float gradUnitPx = heightPx / (float)(gradMax - gradMin);
  const int currentPosPx = (currentValue - gradMin) * gradUnitPx;

  glColor3f(0.0, 1.0, 0.0);

  glPushMatrix();
  glTranslated(50, 0, 0);

  glBegin(GL_LINES);

  /* Draw vertical axis */
  glVertex2i(0, 0);
  glVertex2i(0, heightPx);

  /* Draw horizontal graduations */
  for (int grad = 0; grad <= gradMax - gradMin; grad += gradStep)
  {
    int y = grad * gradUnitPx;
    glVertex2i(0, y);
    glVertex2i(width, y);
  }

  glEnd();

  /* Draw graduation labels */
  for (int grad = gradMin; grad <= gradMax; grad += gradStep)
  {
    int y = (grad - gradMin) * gradUnitPx;
    glRasterPos2i(width + 5, y + gradLabelYOffset);
    printText("%i", grad);
  }

  glPopMatrix();

  /* Draw current value */
  glPushMatrix();
  glTranslated(0, currentPosPx, 0);

  glRasterPos2i(0, gradLabelYOffset);
  printText("%.0f", currentValue);

  glTranslated(40, 0, 0);

  glBegin(GL_TRIANGLES);
  glVertex2i(10, 0);
  glVertex2i(0, -5);
  glVertex2i(0, 5);
  glEnd();

  glPopMatrix();
}

void drawHUDSeparator()
{
  glColor3f(0.0, 1.0, 0.0);

  glBegin(GL_LINES);
  glVertex2i(0, 0);
  glVertex2i(150, 0);
  glEnd();
}

void drawHUD()
{
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glTranslated(50, 0, 0);

  glTranslated(0, glutGet(GLUT_WINDOW_HEIGHT) * 0.1, 0);

  glColor3f(0.0, 1.0, 0.0);

  glRasterPos2i(0, 120);
  printStatus("[V] V-SYNC: ", vsyncIsAvailable(&app.vsyncController), vsyncIsEnabled(&app.vsyncController));
  printStatus("[G] G-SYNC: ", gsyncIsAvailable(&app.gsyncController), gsyncIsAllowed(&app.gsyncController));
  printText("\n");
  printText("[UP] / [DOWN] Max frame rate: %i\n", app.frameRateController.frameRateMax);
  printText("[PGUP] / [PGDOWN] Min frame rate: %i\n", app.frameRateController.frameRateMin);
  printText("[Q] / [ESC] Quit");

  glTranslated(0, 180, 0);

  drawHUDSeparator();

  glTranslated(0, 60, 0);

  drawGauge(glutGet(GLUT_WINDOW_HEIGHT) * 0.8 - 240,
            app.frameRateController.frameRateMin,
            app.frameRateController.frameRateMax,
            10,
            app.frameRateController.currentSimulatedFrameRate);
}

/**
* GLUT's reshape callback function.
* Update the viewport and the projection matrix.
*/
void reshape(int width, int height)
{
  if (height == 0)
  {
    height = 1;
  }

  glViewport(0, 0, (GLsizei)width, (GLsizei)height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, width, 0, height, -1.0f, 1.0f);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glutPostRedisplay();
}

/**
* GLUT's timer callback function.
* Force redraw of the main OpenGL scene.
*/
void timerCallBack(int value)
{
  updateClock(&app.clock);
  computeNextFrameDelayMsec(&app.frameRateController, app.clock.currentTimeSec);

  /* Register next redraw */
  glutTimerFunc(app.frameRateController.nextFrameDelaySec * 1000, timerCallBack, value);

  /* Redraw scene */
  glutPostRedisplay();
}

/**
* GLUT's display callback function.
* Render the main OpenGL scene.
*/
void display()
{
  glClear(GL_COLOR_BUFFER_BIT);

  drawScene();
  drawHUD();

  glutSwapBuffers();
}

/**
* GLUT's Key press callback function.
* Called when user press a key.
*/
void keyPress(unsigned char key, int x, int y)
{
  switch (key)
  {
  case 27: /* Escape */
  case 'q': glutLeaveMainLoop();
    break;
  case 'v': toggleVSync(&app);
    break;
  case 'g': toggleGSync(&app);
    break;
  }
}

/**
* GLUT's Key press callback function.
* Called when user press a special key.
*/
void specialKeyPress(int key, int x, int y)
{
  switch (key)
  {
  case GLUT_KEY_UP: increaseMaxFrameRate(&app.frameRateController, 10);
    break;
  case GLUT_KEY_DOWN: decreaseMaxFrameRate(&app.frameRateController, 10);
    break;
  case GLUT_KEY_PAGE_UP: increaseMinFrameRate(&app.frameRateController, 10);
    break;
  case GLUT_KEY_PAGE_DOWN: decreaseMinFrameRate(&app.frameRateController, 10);
    break;
  }
}

#endif

int main(int, char**)
{
  // Init deps
  /* Application initialization */
  SDL_Init(SDL_INIT_VIDEO);

  // Time the rectangle will travel to the edge in second
  // the bigger value the slower it will move.


  // TODO Query available displays and their features
  // SDL_DisplayID displayID;
  // const SDL_DisplayMode *displayMode = SDL_GetCurrentDisplayMode();

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

  FrameContext frameCtx;

  MyAppl app(window);

  app.init();
  app.run();

  return 0;
}
