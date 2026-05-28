# display (second occurrence - merged)
- For the display driver and font: maintain a Python generator script under main/components/font/. Confidence: 0.75
- Use C++ static_cast instead of C-style casts. Confidence: 0.70
- Use `std::function` for menu callbacks. Confidence: 0.70
- Use `uint8_t` for framebuffer byte type. Confidence: 0.70
- For display framebuffer operations: mark pages dirty with MarkDirty(page) for partial updates. Confidence: 0.70
- In code review: verify bitmap byte ordering matches COM scan direction before approving display changes. Confidence: 0.70
