#pragma once

namespace p4vgit
{
class GuiDrawData
{
public:
    GuiDrawData() = default;
    explicit GuiDrawData(void* native_draw_data)
        : native_draw_data_(native_draw_data)
    {
    }

    void* NativeDrawData() const { return native_draw_data_; }

private:
    void* native_draw_data_ = nullptr;
};
}
