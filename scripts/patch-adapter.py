"""Apply reviewed, narrow patches to the immutable upstream source archive."""
from pathlib import Path
import sys

root = Path(sys.argv[1])

def edit(path, old, new):
    file = root / path
    text = file.read_text(encoding="utf-8")
    if text.count(old) != 1:
        raise SystemExit(f"Expected exactly one patch anchor in {path}: {old[:60]}")
    file.write_text(text.replace(old, new), encoding="utf-8", newline="\n")

edit("src/comobjects.rs", "    fn drop_services(&self)", "    pub(crate) fn drop_services(&self)")
edit("src/comobjects.rs", "    fn get_idesktops_array(&self)", '''    pub(crate) fn move_helper_desktop(&self, from: u32, to: u32) -> Result<()> {
        let manager = self.get_manager_internal()?;
        let desktop = self.get_idesktop(&DesktopInternal::Index(from))?;
        unsafe { manager.move_desktop(ComIn::new(&desktop), to).as_result() }
    }

    fn get_idesktops_array(&self)''')

listener = "src/listener.rs"
edit(listener, "use std::time::Duration;\n", "")
edit(listener, "use windows::Win32::Foundation::HWND;", '''use windows::Win32::Foundation::HWND;
use windows::Win32::System::Com::{CoInitializeEx, CoUninitialize, COINIT_MULTITHREADED};

struct NotificationApartment;
impl Drop for NotificationApartment {
    fn drop(&mut self) { unsafe { CoUninitialize(); } }
}''')
edit(listener, "use windows::Win32::System::Threading::{\n    GetCurrentThread, SetThreadPriority, THREAD_PRIORITY_TIME_CRITICAL,\n};\n", "")
file = root / listener
text = file.read_text(encoding="utf-8")
start = text.index("        // Channel for quitting")
end = text.index("    /// Stops the listener", start)
text = text[:start] + '''        let (tx, rx) = std::sync::mpsc::channel::<DekstopEventThreadMsg>();
        let (ready_tx, ready_rx) = std::sync::mpsc::sync_channel(1);
        let notification_thread = std::thread::spawn(move || {
            let initialized = unsafe { CoInitializeEx(None, COINIT_MULTITHREADED) };
            if initialized.is_err() {
                let _ = ready_tx.send(Err(crate::Error::ComError(initialized)));
                return;
            }
            // Release proxies before balancing this thread's COM initialization.
            let _apartment = NotificationApartment;
            let com_objects = ComObjects::new();
            let listener = match VirtualDesktopNotificationWrapper::new(
                &com_objects, Box::new(move |event| sender.try_send(event.into())),
            ) {
                Ok(listener) => listener,
                Err(error) => { let _ = ready_tx.send(Err(error)); return; }
            };
            let _ = ready_tx.send(Ok(()));
            // No health-check timer. The host reconnects on Explorer restart/errors.
            let _ = rx.recv();
            drop(listener);
        });
        match ready_rx.recv() {
            Ok(Ok(())) => Ok(DesktopEventThread {
                thread_control_sender: Some(tx), thread: Some(notification_thread),
            }),
            result => {
                let _ = notification_thread.join();
                match result {
                    Ok(Err(error)) => Err(error),
                    _ => Err(crate::Error::RpcServerNotAvailable),
                }
            }
        }
    }

''' + text[end:]
file.write_text(text, encoding="utf-8", newline="\n")
edit(listener, "Result<Pin<Box<VirtualDesktopNotificationWrapper>>>", "Result<Pin<Box<VirtualDesktopNotificationWrapper<'a>>>>")

dll = "dll/src/lib.rs"
file = root / dll
text = file.read_text(encoding="utf-8")
start = text.index("                    match item {")
end = text.index("                }\n            });", start)
text = text[:start] + '''                    if matches!(item, DesktopEvent::DesktopChanged { .. }
                        | DesktopEvent::DesktopCreated(_) | DesktopEvent::DesktopDestroyed { .. }
                        | DesktopEvent::DesktopMoved { .. }) {
                        // Invalidation only: no extra COM calls in the forwarding thread.
                        let listeners: Vec<isize> = LISTENER_HWNDS.lock().unwrap().iter().copied().collect();
                        for hwnd in listeners {
                            unsafe { let _ = PostMessageW(HWND(hwnd as _), message_offset, WPARAM(0), LPARAM(0)); }
                        }
                    }
''' + text[end:]
start = text.index('pub extern "C" fn UnregisterPostMessageHook')
end = text.index('#[no_mangle]', start)
text = text[:start] + '''pub extern "C" fn UnregisterPostMessageHook(listener_hwnd: HWND) {
    let empty = {
        let mut listeners = LISTENER_HWNDS.lock().unwrap();
        listeners.remove(&(listener_hwnd.0 as isize));
        listeners.is_empty()
    };
    if empty {
        let workers = { SENDER_THREAD.lock().unwrap().take() };
        if let Some((mut sender, forwarder)) = workers {
            // Neither mutex is held while waiting for either thread.
            let _ = sender.stop();
            let _ = forwarder.join();
        }
    }
}
''' + text[end:]
text += '''
// Host ABI marker: reject the older incompatible/polling adapter before COM calls.
#[no_mangle]
pub extern "C" fn GetHelperAdapterVersion() -> i32 { 2 }

#[no_mangle]
pub extern "C" fn ResetHelperDesktopService() { reset_helper_services(); }

#[no_mangle]
pub extern "C" fn MoveHelperDesktop(from: u32, to: u32) -> i32 {
    move_helper_desktop(from, to).map_or(-1, |_| 1)
}
'''
file.write_text(text, encoding="utf-8", newline="\n")
edit(dll, '''                Err(_er) => {
                    #[cfg(debug_assertions)]''', '''                Err(_er) => {
                    LISTENER_HWNDS.lock().unwrap().remove(&(listener_hwnd.0 as isize));
                    let _ = listener_thread.join();
                    #[cfg(debug_assertions)]''')
with (root / "src/lib.rs").open("a", encoding="utf-8") as file:
    file.write('''
/// Called on the host's one desktop worker, after unregistering notifications.
pub fn reset_helper_services() {
    comobjects::with_com_objects(|objects| { objects.drop_services(); Ok(()) }).ok();
}

pub fn move_helper_desktop(from: u32, to: u32) -> Result<()> {
    comobjects::with_com_objects(move |objects| objects.move_helper_desktop(from, to))
}
''')
