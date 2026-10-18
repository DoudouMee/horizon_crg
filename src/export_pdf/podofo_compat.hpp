#pragma once
#include "util/podofo_inc.hpp"

namespace horizon {
inline void pdf_set_fill_color(PoDoFo::PdfPainter &painter, const PoDoFo::PdfColor &color)
{
#if PODOFO_VERSION_MAJOR >= 1
    painter.GraphicsState.SetNonStrokingColor(color);
#else
    painter.GraphicsState.SetFillColor(color);
#endif
}
inline void pdf_set_stroke_color(PoDoFo::PdfPainter &painter, const PoDoFo::PdfColor &color)
{
#if PODOFO_VERSION_MAJOR >= 1
    painter.GraphicsState.SetStrokingColor(color);
#else
    painter.GraphicsState.SetStrokeColor(color);
#endif
}
inline void pdf_transform(PoDoFo::PdfPainter &painter, double a, double b, double c, double d, double e, double f)
{
#if PODOFO_VERSION_MAJOR >= 1
    painter.GraphicsState.ConcatenateTransformationMatrix(PoDoFo::Matrix(a, b, c, d, e, f));
#else
    painter.GraphicsState.SetCurrentMatrix(PoDoFo::Matrix::FromCoefficients(a, b, c, d, e, f));
#endif
}
} // namespace horizon
