#include <esphome/components/display/display.h>
#include <esphome/components/image/image.h>
#include <esphome/core/helpers.h>
#include <esphome/core/log.h>

#include <cmath>
#include <cstdint>
#include <vector>

class FloydSteinbergDither {
 private:
  struct Error {
    int16_t r;
    int16_t g;
    int16_t b;

    Error() : r(0), g(0), b(0) {}
  };

  // Image adjustment parameters
  float img_adjust_ = false;
  float gamma_ = 1.0f;
  int brightness_ = 0;
  float contrast_ = 1.0f;

  // Color clamping helper
  static uint8_t clamp(int value) { return static_cast<uint8_t>(value < 0 ? 0 : (value > 255 ? 255 : value)); }

  // Pixel preprocessing pipeline
  Color preprocess_pixel(Color orig) {
    // Gamma correction
    auto gamma_correct = [this](uint8_t channel) {
      return clamp(static_cast<int>(pow(channel / 255.0f, gamma_) * 255 + 0.5f));
    };

    // Contrast adjustment
    auto contrast_adjust = [this](uint8_t channel) {
      return clamp(static_cast<int>((channel - 128) * contrast_ + 128));
    };

    // Brightness adjustment
    auto brightness_adjust = [this](uint8_t channel) { return clamp(channel + brightness_); };

    // Processing order: Gamma -> Contrast -> Brightness
    orig.r = gamma_correct(orig.r);
    orig.g = gamma_correct(orig.g);
    orig.b = gamma_correct(orig.b);

    orig.r = contrast_adjust(orig.r);
    orig.g = contrast_adjust(orig.g);
    orig.b = contrast_adjust(orig.b);

    orig.r = brightness_adjust(orig.r);
    orig.g = brightness_adjust(orig.g);
    orig.b = brightness_adjust(orig.b);

    return orig;
  }

  // Palette matching algorithm
  Color find_closest(const Color &adjusted, const std::vector<Color> &palette) {
    int min_dist = INT_MAX;
    Color closest = palette[0];

    for (const auto &c : palette) {
      // Weighted color distance calculation
      int dist = abs(adjusted.r - c.r) * 3 + abs(adjusted.g - c.g) * 5 + abs(adjusted.b - c.b) * 2;

      if (dist < min_dist) {
        min_dist = dist;
        closest = c;
      }
    }
    return closest;
  }

 public:
  // Configuration interface
  void configure(bool img_adjust, float gamma, int brightness, float contrast) {
    img_adjust_ = img_adjust;
    gamma_ = std::fmax(gamma, 0.1f);
    brightness_ = brightness;
    contrast_ = std::fmax(contrast, 0.0f);
  }

  void draw_dither_image(display::Display &it, int start_x, int start_y, const image::Image &img,
                         const std::vector<Color> &palette, Color color_on = COLOR_ON, Color color_off = COLOR_OFF) {
    const int img_w = img.get_width();
    const int img_h = img.get_height();
    const int disp_w = it.get_width();
    const int disp_h = it.get_height();

    std::vector<Error> current_errors(img_w);
    std::vector<Error> next_errors(img_w);

    for (int y = 0; y < img_h; ++y) {
      std::fill(next_errors.begin(), next_errors.end(), Error());

      for (int x = 0; x < img_w; ++x) {
        const int dx = start_x + x;
        const int dy = start_y + y;

        // Boundary checking
        if (dx < 0 || dx >= disp_w || dy < 0 || dy >= disp_h) continue;

        Color orig = img.get_pixel(x, y);

        // Apply preprocessing based on img_adjust flag
        if (img_adjust_) {
          orig = preprocess_pixel(orig);
        }

        // Error diffusion handling
        const Error &err = current_errors[x];
        Color adjusted{clamp(orig.r + err.r), clamp(orig.g + err.g), clamp(orig.b + err.b)};

        // Palette selection
        Color selected = find_closest(adjusted, palette);
        if (it.get_display_type() == DisplayType::DISPLAY_TYPE_BINARY) {
          if (selected == Color::BLACK) {
            it.draw_pixel_at(dx, dy, color_on);
          } else {
            it.draw_pixel_at(dx, dy, color_off);
          }
        } else {
          it.draw_pixel_at(dx, dy, selected);
        }

        // Calculate quantization errors
        const int16_t err_r = adjusted.r - selected.r;
        const int16_t err_g = adjusted.g - selected.g;
        const int16_t err_b = adjusted.b - selected.b;

        // Error diffusion kernel
        auto diffuse_error = [&](Error &e, int16_t r, int16_t g, int16_t b, int factor) {
          e.r += static_cast<int16_t>(r * factor / 16);
          e.g += static_cast<int16_t>(g * factor / 16);
          e.b += static_cast<int16_t>(b * factor / 16);
        };

        // Right pixel
        if (x + 1 < img_w) {
          diffuse_error(current_errors[x + 1], err_r, err_g, err_b, 7);
        }

        // Bottom pixels (next line)
        if (y + 1 < img_h) {
          if (x > 0) {
            diffuse_error(next_errors[x - 1], err_r, err_g, err_b, 3);
          }
          diffuse_error(next_errors[x], err_r, err_g, err_b, 5);
          if (x + 1 < img_w) {
            diffuse_error(next_errors[x + 1], err_r, err_g, err_b, 1);
          }
        }
      }
      current_errors.swap(next_errors);
    }
  }

  void draw_dither_image(display::Display &it, int x, int y, const image::Image &img, display::ImageAlign align,
                         const std::vector<Color> &palette, Color color_on, Color color_off) {
    auto x_align = display::ImageAlign(int(align) & (int(display::ImageAlign::HORIZONTAL_ALIGNMENT)));
    auto y_align = display::ImageAlign(int(align) & (int(display::ImageAlign::VERTICAL_ALIGNMENT)));

    switch (x_align) {
      case ImageAlign::RIGHT:
        x -= img.get_width();
        break;
      case ImageAlign::CENTER_HORIZONTAL:
        x -= img.get_width() / 2;
        break;
      case ImageAlign::LEFT:
      default:
        break;
    }

    switch (y_align) {
      case ImageAlign::BOTTOM:
        y -= img.get_height();
        break;
      case ImageAlign::CENTER_VERTICAL:
        y -= img.get_height() / 2;
        break;
      case ImageAlign::TOP:
      default:
        break;
    }

    draw_dither_image(it, x, y, img, palette, color_on, color_off);
  }
};