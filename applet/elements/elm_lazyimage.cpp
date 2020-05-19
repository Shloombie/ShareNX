#include "elm_lazyimage.hpp"

#include "mediaview.hpp"

#include <memory>

LazyImage::LazyImage(const CapsAlbumFileId &id) : Image(), file_id(id) {
    this->registerAction("OK", brls::Key::A, [this] {
        brls::Application::pushView(new AlbumView(this->file_id));
        return true;
    });
    this->setScaleType(brls::ImageScaleType::SCALE);
}

LazyImage::~LazyImage() {
    if (this->videoLength)
        delete[] this->videoLength;
}

void LazyImage::draw(NVGcontext *vg, int x, int y, unsigned width, unsigned height, brls::Style *style, brls::FrameContext *ctx) {
    if (!loading) {
        this->LoadImage();
    } else if (loading && ready) {
        this->setRGBAImage(320, 180, tmpBuffer);
        delete[] tmpBuffer;
        this->layout(brls::Application::getNVGContext(), style, ctx->fontStash);
        this->invalidate();
        ready = false;
    }

    float cornerRadius = (float)style->Button.cornerRadius;

    float shadowWidth = 2.0f;
    float shadowFeather = 10.0f;
    float shadowOpacity = 63.75f;
    float shadowOffset = 10.0f;

    NVGpaint shadowPaint = nvgBoxGradient(vg,
                                          x, y + shadowWidth,
                                          width, height,
                                          cornerRadius * 2, shadowFeather,
                                          RGBA(0, 0, 0, shadowOpacity * alpha), brls::transparent);

    nvgBeginPath(vg);
    nvgRect(vg, x - shadowOffset, y - shadowOffset,
            width + shadowOffset * 2, height + shadowOffset * 3);
    nvgRoundedRect(vg, x, y, width, height, cornerRadius);
    nvgPathWinding(vg, NVG_HOLE);
    nvgFillPaint(vg, shadowPaint);
    nvgFill(vg);

    Image::draw(vg, this->x, this->y, this->width, this->height, style, ctx);

    // Video length. 54x18
    if (this->videoLength) {
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 0.5 * 0xff));
        nvgBeginPath(vg);
        nvgRect(vg, x + width - 54, y + height - 18, 54, 18);
        nvgFill(vg);

        nvgFillColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
        nvgFontSize(vg, 14);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgBeginPath(vg);
        nvgText(vg, x + width - 27, y + height - 9, this->videoLength, nullptr);
        nvgFill(vg);
    }
}

brls::View *LazyImage::getDefaultFocus() {
    return this;
}

void LazyImage::SetAlbumThumbnailImage(unsigned char *buffer, char *length) {
    this->tmpBuffer = buffer;
    this->videoLength = length;
    this->ready = true;
}

#include <memory>
#include <mutex>
#include <queue>

namespace {

    struct ImageLoaderTask {
        const CapsAlbumFileId &fileId;
        LazyImage *image;

        void make() const {
            size_t workSize = 0x10000;
            auto work = std::make_unique<u8[]>(workSize);
            size_t imgSize = 320 * 180 * 4;
            auto img = new u8[imgSize];

            Result rc = 0;
            u64 w, h;
            char *videoLength = nullptr;
            if (hosversionBefore(4, 0, 0)) {
                rc = capsaLoadAlbumScreenShotThumbnailImage(&w, &h, &this->fileId, img, imgSize, work.get(), workSize);
            } else {
                CapsScreenShotDecodeOption opts = {};
                CapsScreenShotAttribute attrs = {};
                rc = capsLoadAlbumScreenShotThumbnailImageEx0(&w, &h, &attrs, &this->fileId, &opts, img, imgSize, work.get(), workSize);

                /* Round video length to nearest full number. */
                u8 length = (attrs.length_x10 + 499) / 1000;
                if (length) {
                    videoLength = new char[8];
                    std::sprintf(videoLength, "%dsec", length);
                }
            }
            if (R_SUCCEEDED(rc)) {
                image->SetAlbumThumbnailImage(img, videoLength);
            } else
                brls::Logger::error("Failed to load image with: 0x%x", rc);
        }
    };

    class ImageLoader {
      private:
        Thread thread;
        std::queue<ImageLoaderTask> tasks;
        bool exitflag = false;
        std::mutex mtx;

        static void WorkThreadFunc(void *user) {
            ImageLoader *ptr = static_cast<ImageLoader *>(user);
            while (appletMainLoop()) {
                if (ptr->Loop())
                    continue;

                {
                    std::scoped_lock lk(ptr->mtx);
                    if (ptr->exitflag)
                        break;
                }

                svcSleepThread(1'000'000);
            }
        }

        bool Loop() {
            const ImageLoaderTask *task = nullptr;
            {
                std::scoped_lock lk(mtx);
                if (tasks.empty())
                    return false;

                task = &tasks.front();
            }

            if (task) {
                task->make();

                std::scoped_lock lk(mtx);
                tasks.pop();
            }
            return true;
        }

      public:
        ImageLoader() {
            threadCreate(&thread, WorkThreadFunc, this, nullptr, 0x1000, 0x2c, -2);
            threadStart(&thread);
        }

        ~ImageLoader() {
            exitflag = true;
            threadWaitForExit(&thread);
            threadClose(&thread);
        }

        void Enqueue(const CapsAlbumFileId &fileId, LazyImage *image) {
            std::scoped_lock lk(mtx);
            tasks.emplace(fileId, image);
        }
    };

    ImageLoader g_loader;
}

void LazyImage::LoadImage() {
    g_loader.Enqueue(this->file_id, this);
    this->loading = true;
}