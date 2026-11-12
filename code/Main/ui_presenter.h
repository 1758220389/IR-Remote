#ifndef UI_PRESENTER_H
#define UI_PRESENTER_H
#include "ui.h"
#include "ui_view.h"
typedef struct { UiViewModel view; char title[32]; } UiPresentation;
void UiPresenter_Build(const UiState *state, UiPresentation *presentation);
#endif
