#ifndef __MY_DRAWING_AREA_H__
#define __MY_DRAWING_AREA_H__

#include <gtkmm.h>

// OpenGL drawing surface.
//
// This replaces the old gtkglextmm Gtk::GL::DrawingArea with a Gtk::GLArea,
// which is built into GTK 3.  The actual scene is drawn by
// SpectrumAnalyzerWindow via the signal_render() callback.
class MyDrawingArea : public Gtk::GLArea
{
public:
	MyDrawingArea();
	virtual ~MyDrawingArea();

	// Schedule a repaint of the OpenGL contents.
	void trigger_redraw();

protected:
	virtual void on_realize() override;
	virtual void on_resize(int width, int height) override;
};

#endif
