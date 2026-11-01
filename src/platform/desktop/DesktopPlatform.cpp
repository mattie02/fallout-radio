#include "platform/desktop/DesktopPlatform.h"

#include <sys/stat.h>

#include <cstdio>
#include <ctime>

namespace radio {

// --- Audio ------------------------------------------------------------------

DesktopAudio::~DesktopAudio() {
    if (dev_) SDL_CloseAudioDevice(dev_);
}

bool DesktopAudio::begin(uint32_t sampleRate) {
    SDL_AudioSpec want{};
    want.freq = static_cast<int>(sampleRate);
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
    if (!dev_) {
        std::fprintf(stderr, "audio: %s\n", SDL_GetError());
        return false;
    }
    targetQueueFrames_ = sampleRate / 10;  // keep ~100 ms buffered
    SDL_PauseAudioDevice(dev_, 0);
    return true;
}

size_t DesktopAudio::availableFrames() {
    if (!dev_) return 0;
    const uint32_t queued = SDL_GetQueuedAudioSize(dev_) / sizeof(int16_t);
    return queued >= targetQueueFrames_ ? 0 : targetQueueFrames_ - queued;
}

size_t DesktopAudio::write(const int16_t* samples, size_t frames) {
    if (!dev_) return 0;
    if (SDL_QueueAudio(dev_, samples, static_cast<uint32_t>(frames * sizeof(int16_t))) != 0)
        return 0;
    return frames;
}

// --- Input ------------------------------------------------------------------

InputState DesktopInput::read() {
    InputState s = pending_;
    pending_ = {};
    return s;
}

bool DesktopInput::handleEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_MOUSEWHEEL: {
            int y = e.wheel.y;
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) y = -y;
            pending_.dialSteps += y;
            return true;
        }
        case SDL_KEYDOWN: {
            const int step = (e.key.keysym.mod & KMOD_SHIFT) ? 10 : 1;
            switch (e.key.keysym.sym) {
                case SDLK_LEFT: pending_.dialSteps -= step; return true;
                case SDLK_RIGHT: pending_.dialSteps += step; return true;
                case SDLK_SPACE:
                    if (!e.key.repeat) pending_.pressed = true;
                    return true;
                default: return false;
            }
        }
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                pending_.pressed = true;
                return true;
            }
            return false;
        default:
            return false;
    }
}

// --- Display ----------------------------------------------------------------

DesktopDisplay::~DesktopDisplay() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
}

bool DesktopDisplay::begin() {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // nearest-neighbour
    const int w = kDisplayWidth * kScale + 2 * kBezel;
    const int h = kDisplayHeight * kScale + 2 * kBezel;
    window_ = SDL_CreateWindow("Vintage Radio", SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, w, h, 0);
    if (!window_) {
        std::fprintf(stderr, "display: %s\n", SDL_GetError());
        return false;
    }
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) {
        std::fprintf(stderr, "display: %s\n", SDL_GetError());
        return false;
    }
    SDL_RenderSetLogicalSize(renderer_, w, h);  // stay sharp on Retina
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING, kDisplayWidth, kDisplayHeight);
    if (!texture_) {
        std::fprintf(stderr, "display: %s\n", SDL_GetError());
        return false;
    }
    refresh();
    return true;
}

void DesktopDisplay::show(const uint8_t* buffer) {
    SDL_memcpy(last_, buffer, sizeof(last_));
    refresh();
}

void DesktopDisplay::refresh() {
    if (!renderer_) return;
    constexpr uint32_t kOn = 0xFFE8F0FF;   // SSD1306 white, slightly cool
    constexpr uint32_t kOff = 0xFF050608;  // unlit panel

    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(texture_, nullptr, &pixels, &pitch) == 0) {
        for (int y = 0; y < kDisplayHeight; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pixels) + y * pitch);
            for (int x = 0; x < kDisplayWidth; ++x) {
                const bool on = last_[x + (y / 8) * kDisplayWidth] & (1u << (y % 8));
                row[x] = on ? kOn : kOff;
            }
        }
        SDL_UnlockTexture(texture_);
    }

    SDL_SetRenderDrawColor(renderer_, 24, 24, 26, 255);  // bezel
    SDL_RenderClear(renderer_);
    const SDL_Rect dst{kBezel, kBezel, kDisplayWidth * kScale, kDisplayHeight * kScale};
    SDL_RenderCopy(renderer_, texture_, nullptr, &dst);
    SDL_RenderPresent(renderer_);
}

void DesktopDisplay::setTitle(const std::string& title) {
    if (window_) SDL_SetWindowTitle(window_, title.c_str());
}

// --- Clock ------------------------------------------------------------------

bool DesktopClock::begin() {
    if (anchorTicks_ == 0) resetToRealTime();
    return true;
}

int64_t DesktopClock::now() {
    if (paused_) return static_cast<int64_t>(anchorTime_);
    const double elapsed = (SDL_GetTicks64() - anchorTicks_) / 1000.0;
    return static_cast<int64_t>(anchorTime_ + elapsed * speed_);
}

uint32_t DesktopClock::millis() { return static_cast<uint32_t>(SDL_GetTicks64()); }

void DesktopClock::reanchor() {
    const uint64_t ticks = SDL_GetTicks64();
    if (!paused_) anchorTime_ += (ticks - anchorTicks_) / 1000.0 * speed_;
    anchorTicks_ = ticks;
}

void DesktopClock::setTime(int64_t localSeconds) {
    anchorTime_ = static_cast<double>(localSeconds);
    anchorTicks_ = SDL_GetTicks64();
}

void DesktopClock::setPaused(bool paused) {
    reanchor();
    paused_ = paused;
}

void DesktopClock::setSpeed(double speed) {
    reanchor();
    speed_ = speed;
}

void DesktopClock::resetToRealTime() {
    paused_ = false;
    speed_ = 1.0;
    setTime(realLocalTime());
}

int64_t DesktopClock::realLocalTime() {
    const std::time_t t = std::time(nullptr);
    std::tm local{};
    localtime_r(&t, &local);
    return static_cast<int64_t>(timegm(&local));
}

bool DesktopClock::parseTime(const char* text, int64_t& out) {
    std::tm tm{};
    int sec = 0;
    const int n = std::sscanf(text, "%d-%d-%d %d:%d:%d", &tm.tm_year, &tm.tm_mon,
                              &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &sec);
    if (n < 5) return false;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_sec = sec;
    out = static_cast<int64_t>(timegm(&tm));
    return true;
}

std::string DesktopClock::format(int64_t localSeconds) {
    const std::time_t t = static_cast<std::time_t>(localSeconds);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%a %Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

// --- Storage ----------------------------------------------------------------

namespace {

class DesktopFile : public File {
public:
    explicit DesktopFile(std::FILE* f) : f_(f) {
        std::fseek(f_, 0, SEEK_END);
        size_ = static_cast<uint32_t>(std::ftell(f_));
        std::fseek(f_, 0, SEEK_SET);
    }
    ~DesktopFile() override { std::fclose(f_); }

    size_t read(uint8_t* dst, size_t len) override { return std::fread(dst, 1, len, f_); }
    bool seek(uint32_t pos) override { return std::fseek(f_, pos, SEEK_SET) == 0; }
    uint32_t position() override { return static_cast<uint32_t>(std::ftell(f_)); }
    uint32_t size() override { return size_; }

private:
    std::FILE* f_;
    uint32_t size_ = 0;
};

}  // namespace

bool DesktopStorage::begin() {
    struct stat st {};
    if (stat(root_.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        std::fprintf(stderr, "storage: content folder '%s' not found\n", root_.c_str());
        return false;
    }
    return true;
}

std::string DesktopStorage::resolve(const char* path) const {
    std::string p = root_;
    if (path[0] != '/') p += '/';
    return p + path;
}

bool DesktopStorage::exists(const char* path) {
    struct stat st {};
    return stat(resolve(path).c_str(), &st) == 0;
}

std::unique_ptr<File> DesktopStorage::open(const char* path) {
    std::FILE* f = std::fopen(resolve(path).c_str(), "rb");
    if (!f) return nullptr;
    return std::make_unique<DesktopFile>(f);
}

}  // namespace radio
