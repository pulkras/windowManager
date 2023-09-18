#ifndef WINDOW_MANAGER_HPP
#define WINDOW_MANAGER_HPP

extern "C" {
#include <X11/Xlib.h>
}
#include <memory>
#include <string>
#include <unordered_map>
class WindowManager {
	public:
		// estabilish connection to an X server
		// creating a WindowManager instance

		static ::std::unique_ptr<WindowManager> Create(
				const std::string& display_str = std::string()
		};

		// disconnect from the X server
		~WindowManager();
		// enters the main event loop
		void Run();

	private:
		// invoked internally by Create() function
		WindowManager(Display* display);

		// frames a top-level window
		void Frame(Window w, bool was_created_before_window_manager);
		// unframes a clinet window
		void Unframe(Window w);

		// event handlers
		void OnCreateNotify(const XcreateWindowEvent& e);
		void OnDestroyNotify(const XdestroyWindowEvent& e);
		void OnReparentNotify(const XReparentEvent& e);
		void OnMapRequest(const XMapRequestEvent& e);
		void OnMapNotify(const XMapEvent& e);
		void OnUnmapNotify(const XUnmapEvent& e);
		void OnConfigureRequest(const XConfigureRequest& e);
		void OnConfigureNotify(const XConfigureEvent& e);


		// handle to the underlying Xlib Display struct
		Display* display_;
		// handle to root window
		const Window root_;
		// maps top-level windows to their frame windows
		::std::unordered_map<Window, Window> clients_;
		// xlib error handler. it's address is passed to xlib
		static int OnXError(Display* display, XErrorEvent* e);
		// xlib error handler used to determine whether another window manager

		static int OnWMDetected(Display* display, XErrorEvent* e);
		// whether an existing window maanger has been detected. set by OnWMDetected
		// hence must be static
		static bool wm_detected_;
};

#endif
