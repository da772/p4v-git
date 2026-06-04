#include "ui/AppUi.h"

#include "ui/widgets/Widgets.h"

namespace p4vgit
{
void AppUi::Draw()
{
    ui::widgets::DrawDockspace();

    if (ui::widgets::BeginWindow("Workspace"))
    {
        ui::widgets::DrawWindowHeader("Workspace");
        ui::widgets::Text("Repository workspace will appear here.");
    }
    ui::widgets::EndWindow();

    if (m_showDemoWindow)
        ui::widgets::ShowDemoWindow(&m_showDemoWindow);
}
}
