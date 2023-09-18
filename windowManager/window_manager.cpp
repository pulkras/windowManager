#include "window_manager.hpp"
extern "C" {
#include <X11/Xutil.h>
}
#include <glog/logging.h>
#include <cstring>
#include <algorithm>
using ::std::string;
using ::std::unique_ptr;

bool WindowManager::wm_detected_;

unique_ptr<WindowManager> WindowManager::Create() {
	// first is open X display
	Display* display = XOpenDisplay(nullptr);
	if (display == nullptr) {
		LOG(ERROR) << "Failed to open X display" << XDisplayName(nullptr);
		return nullptr;
	}
	// second is construct WindowManager instance
	return unique_ptr<WindowManager>(new WindowManager(display));
}

WindowManager::WindowManager(Display* display)
	: display_(CHECK_NOTNULL(display)), 
	  root_(DefaultRootWindow(display_)) {
}

WindowManager::~WindowManager() {
	XCloseDisplay(display_);
}

void WindowManager::Run() {
	// first is initialization
	// a. select events on root window
	// we can exit if another window manager is already running
	wm_detected_ = false;
	XSetErrorHandler(&WindowManager::OnWMDetected);
	XSelectInput(
			display_,
			root_,
			SubstructureRedirectMask | SubstructureNotifyMask);
	XSync(display_, false);
	if (wm_detected_) {
		LOG(ERROR) << "Detected another window manager on display" << XDisplayString(display_);
		return;
	}
	// b. set error handler
	XSetErrorHandler(&WindowManager::OnXError);

	// c. grab X server to prevent windows from other window managers
	XGrabServer(display_);

	// d. frame exising top-level windows
	
	// 1. uery existing top-level windows
	Window returned_root, returned_parent;
	Window* top_level_windows;
	unsigned int num_top_level_windows;
	CHECK(XQueryTree(
				display_,
				root_,
				&returned_root,
				&returned_parent,
				&top_level_windows,
				&num_top_level_windows));
	CHECK_EQ(returned_root, root_);

	// 2. frame each top-level window
	for (unsigned int i = 0; i < num_top_level_windows; i++) {
		Frame(top_level_windows[i], true);
	}

	// 3. free top-level window array
	XFree(top_level_windows);

	// e. ungrap X server
	XUngramServer(display_);

	// second is a main event loop
	for (;;) {
		// get next event
		XEvent e;
		XNextEvent(display_, &e);
		LOG(INFO) << "Received event: ";

		// dispatch event
		switch (e.type) {
			case CreateNotify:
			    OnCreateNotify(e.xcreatewindow);
			    break;
			case DestroyNotify:
				OnDestroyNotify(e.xdestroywindow);
				break;
			case ReparentNotify:
				OnReparentNotify(e.xreparent);
				break;
			case MapRequest:
				OnMapRequest(e.xmaprequest);
				break;
			case MapNotify:
				OnMapNotify(e.xmap);
				break;
			case UnmapNotify:
				OnUnmapNotify(e.xunmap);
				break;
			case ConfigureRequest:
				OnConfigureRequest(e.xconfigurerequest);
				break;
			case ConfigureNotify:
				OnConfigureNotify(e.xconfigure);
				break;
			// etc. etc.
			default:
				LOG(WARNING) << "Ignored event";
		}
	}
}

void WindowManager::OnCreateNotify(const XCreateWindowEvent& e) {}
void WindowManager::OnDestroyNofity(const XDestroyWindowEvent& e) {}
void WindowManager::OnReparentNotify(const XReparentEvent& e) {}

void WindowManager::OnMapRequest(const XMapRequestEvent& e) {
	// frame or reframe window
	Frame(e.window, false);
	// map window
	XMapWindow(display_, e.window);
}

void WindowManager::OnMapNotify(const XMapEvent& e) {}
void WindowManager::OnUnmapNotify(const XUnmapEvent& e) {
	// if it is a client window, unmap it
	if (!clients_.count(e.window)) {
		LOG(INFO) << "ignore UnmapNotify for non-client window " << e.window;
		return;
	}

	if (e.event == root_) {
		LOG(INFO) << "ignore UnmapNotify for reparented pre-existing window " << e.window;
		return;
	}

	Unframe(e.window);
}

void WindowManager::Frame(Window w) {
	// visual properties fo the frame to create it
	const unsigned int BORDER_WIDTH = 3;
	const unsigned long BORDER_COLOR = 0xffff00;
	const unsigned long BG_COLOR = 0x0000ff;

	// retrieve attributes
	XWindowAttributes x_window_attrs;
	CHECK(XGetWindowAttributes(display_, w, &x_window_attrs));

	// if window was created before window manager started, we should frame 
	// it only if it is visible and doesn't set override_redirect
	if (was_created_before_window_manager) {
		if (x_window_attrs.override_redirect || x_window_attrs.map_state != IsViewable) {
			return;
		}
	}
	
	// create frame
	const Window frame = XCreateSimpleWindow(
			display_,
			root_,
			x_window_attrs.x,
			x_window_attrs.y,
			x_window_attrs.width,
			x_window_attrs.height,
			BORDER_WIDTH,
			BORDER_COLOR,
			BG_COLOR);

	// select events on frame
	XselectInput(
			display_,
			frame,
			SubstructureRedirectMask | SubstructureNotifyMask);

	// add client to save set
	XAddToSaveSet(display_, w);

	// reparent client window
	XReparentWindow(
			display_,
			w,
			frame,
			0, 0); // offset of client window within frame

	// map frame
	XMapWindow(display_, frame);

	// save frame handle
	clients_[w] = frame;

	// grab events for window management actions on client window
	// a. Move windows with ...
	// XGrabButton(...);
	// b. resize window with ...
	// XGrabButton(...);
	// c. kill windows with ....
	// XGrabKey(...);
	// switch windows with ...
	// XGrabKey(...)
	
	LOG(INFO) << "framed window " << w << " [" << frame << "]";
}

void WindowManager::Unframe(Window w) {
	// we reverse the steps taken in Frame() function
	const Window frame = clients_[w];
	// unmap frame
	XUnmapWindow(display_, frame);
	
	// reparent client window back to root window
	XReparentWindow(
			display_,
			w,
			root_,
			0, 0);

	// remove client window from save set
	XRemoveFromSaveSet(display_, w);

	// destroy frame
	XDestroyWindow(display_, frame);

	//drop reference to frame handle
	clients.erase(w);

	LOG(INFO) << "unframed window " << w << " [" << frame << "]";
}

void WindowManager::OnConfigureRequest(const XConfigureRequestEvent& e) {
	XWindowChanges changes;
	// copy fields from e to changes
	changes.x = e.x;
	changes.y = e.y;
	changes.width = e.width;
	changes.height = e.height;
	changes.border_width = e.border_width;
	changes.sibling = e.above;
	changes.stack_mode = e.detail;

	if (clients_.count(e.window)) {
		const Window frame = clients_[e.window];
		XConfigureWindow(display_, frame, e.value_mask, &changes);
		LOG(INFO) << "resize [" << frame << "] to " << Size<int>(e.width, e.height);
	}

	//grant request by calling XConfigureWindow()
	XConfigureWindow(display_, e.window, e.value_mask, &changes);
	LOG(INFO) << "Resize " << e.window << " to " << Size<int>(e.width, e.height);
}
int WindowManager::OnWMDetected(Display* display, XErrorEvent* e) {
	// XselectInput is BadAccess. we don't expect this handler to receive other errors
	CHECK_EQ(static_cast<int>(e->error_code), BadAccess);
	// setting flag
	wm_detected_ = true;

	return 0;
}

void WindowManager::OnConfigureNotify(const XConfigureEvent& e) {}

int WindowManager::OnXError(Display* display, XErrorEvent* e) { /* print e */}

