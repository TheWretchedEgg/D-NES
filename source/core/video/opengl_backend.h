#pragma once

#include <mutex>
#include <vector>
#include <memory>

#include "gl_util.h"
#include "igl_platform.h"
#include "video_backend_base.h"

class OpenGLBackend final : public VideoBackendBase
{
public:
    OpenGLBackend(NES& nes, void* windowHandle);
    ~OpenGLBackend();

    void Initialize() override;
    void CleanUp() noexcept override;

    void SubmitFrame(uint8_t* frameBuffer) noexcept override;
    void ShowMessage(const std::string& message, uint32_t duration) noexcept override;    

private:

    void DrawFps(uint32_t fps);
    void DrawMessages();
    void DrawText(const std::string& text, uint32_t xPos, uint32_t yPos);
    void UpdateSurfaceSize();
    void SwapFrameBuffers();


    void* _windowHandle;
    uint32_t _windowWidth;
    uint32_t _windowHeight;
    uint32_t _currentFps{0};

    std::mutex _messageMutex;
    std::vector<std::pair<std::string, std::chrono::steady_clock::time_point> > _messages;

    GLuint _frameProgramId;
    GLuint _frameVertexArrayId;
    GLuint _frameVertexBuffer;
    GLuint _frameTextureId;
    GLuint _textProgramId;
    GLuint _textTextureId;
    GLuint _textVertexBuffer;
    GLuint _textUVBuffer;

    std::unique_ptr<IGLPlatform> _glPlatform;
};