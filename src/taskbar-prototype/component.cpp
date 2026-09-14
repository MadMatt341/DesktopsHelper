#include <windows.h>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <atomic>
#include <vector>
#include <array>
#include "shared.h"
using namespace winrt;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
static HMODULE module;
static std::atomic<bool> attaching=false;
#include "native-root.h"

// UI objects and endpoint are owned on the taskbar thread. The endpoint holds
// one strong reference; its destruction drains the process-wait callback first.
struct Integration : implements<Integration,IUnknown> {
    struct Placement { FrameworkElement element{nullptr}; int column=0,span=1; };
    Grid parent{nullptr};
    StackPanel strip{nullptr};
    ColumnDefinition leftColumn{nullptr},contentColumn{nullptr};
    std::vector<Placement> placements;
    std::array<Primitives::ToggleButton,5> buttons{nullptr,nullptr,nullptr,nullptr,nullptr};
    std::array<TextBlock,5> labels{nullptr,nullptr,nullptr,nullptr,nullptr};
    std::array<event_token,5> clicks{};
    std::array<bool,5> subscribed{};
    HWND control=nullptr,helper=nullptr;
    DWORD helperPid=0;
    HANDLE ownerProcess=nullptr,controllerProcess=nullptr,ownerWait=nullptr;
    ULONGLONG deadline=0;
    UINT request=RegisterWindowMessageW(DH_TASKBAR_REQUEST),state=RegisterWindowMessageW(DH_TASKBAR_STATE);
    unsigned active=DH_TASKBAR_UNAVAILABLE;
    bool mounted=false,removing=false;

    ~Integration() {
        StopOwnerWait();
        if(ownerProcess)CloseHandle(ownerProcess);
        if(controllerProcess)CloseHandle(controllerProcess);
    }
    // Bounded diagnostics, retrieved by the controller. Never write files on
    // Explorer's UI thread (including normal clicks and layout inspection).
    void RecordError(HRESULT error) noexcept {
        if(IsWindow(helper))SetPropW(helper,DH_TASKBAR_ERROR,
            reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(static_cast<DWORD>(error))));
    }
    template<typename Action> void CleanupStep(Action action) noexcept {
        try { action(); } catch(...) { RecordError(to_hresult()); }
    }
    bool OwnerAlive() const noexcept {
        DWORD pid=0;
        return ownerProcess && WaitForSingleObject(ownerProcess,0)==WAIT_TIMEOUT &&
            GetWindowThreadProcessId(helper,&pid) && pid==helperPid;
    }
    void CheckAttachment() {
        if(!OwnerAlive() || !controllerProcess ||
           WaitForSingleObject(controllerProcess,0)!=WAIT_TIMEOUT || GetTickCount64()>=deadline)
            throw hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED));
    }
    void Checkpoint(unsigned stage) {
#ifdef DH_TASKBAR_TESTING
        if(reinterpret_cast<UINT_PTR>(GetPropW(helper,L"DesktopsHelper.TestFailStage"))==stage)
            throw hresult_error(E_ABORT,L"Injected mount failure");
#else
        (void)stage;
#endif
        CheckAttachment();
    }
    void Refresh() {
        for(unsigned i=0;i<buttons.size();i++)if(buttons[i]) {
            buttons[i].IsEnabled(active!=DH_TASKBAR_UNAVAILABLE);
            buttons[i].IsChecked(active==i+1);
            buttons[i].FontWeight(Windows::UI::Text::FontWeight{
                static_cast<unsigned short>(active==i+1?700:400)});
            labels[i].Opacity(active==i+1?1.0:0.62);
        }
        if(control)SetPropW(control,DH_TASKBAR_CURRENT,reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(active)));
    }
    void Request(unsigned desktop) noexcept {
        if(OwnerAlive())PostMessageW(helper,request,desktop,reinterpret_cast<LPARAM>(control));
    }
    bool Inspect() {
        if(!mounted || !OwnerAlive() || !strip || !parent)return false;
        auto origin=strip.TransformToVisual(nullptr).TransformPoint({0,0});
        return strip.ActualWidth()>0 && strip.ActualHeight()>0 && origin.X>=0 && origin.X<32;
    }
    static void CALLBACK OnOwnerExit(void* context,BOOLEAN) noexcept {
        auto self=static_cast<Integration*>(context);
        PostMessageW(self->control,OwnerExited,0,0);
    }
    void StopOwnerWait() noexcept {
        if(ownerWait) {
            // Callback only posts, so draining cannot wait on this UI thread.
            UnregisterWaitEx(ownerWait,INVALID_HANDLE_VALUE);
            ownerWait=nullptr;
        }
    }
    void Remove() noexcept {
        if(removing)return;
        removing=true; mounted=false;
        StopOwnerWait();
        if(control)RemovePropW(control,DH_TASKBAR_MOUNTED);
        Request(DH_TASKBAR_DETACH);
        for(unsigned i=0;i<buttons.size();i++)if(buttons[i] && subscribed[i]) {
            CleanupStep([&] { buttons[i].Click(clicks[i]); });
            subscribed[i]=false;
        }
        CleanupStep([&] {
            uint32_t index=0;
            if(parent && strip && parent.Children().IndexOf(strip,index))parent.Children().RemoveAt(index);
        });
        for(auto const& entry:placements) {
            CleanupStep([&] { Grid::SetColumn(entry.element,entry.column); });
            CleanupStep([&] { Grid::SetColumnSpan(entry.element,entry.span); });
        }
        for(auto const& column:{leftColumn,contentColumn})CleanupStep([&] {
            uint32_t index=0;
            if(parent && column && parent.ColumnDefinitions().IndexOf(column,index))
                parent.ColumnDefinitions().RemoveAt(index);
        });
        placements.clear();
        for(auto& button:buttons)button=nullptr;
        for(auto& label:labels)label=nullptr;
        strip=nullptr; parent=nullptr; leftColumn=nullptr; contentColumn=nullptr;
    }
    void Close() noexcept {
        auto keep=get_strong();
        Remove();
        if(control)DestroyWindow(control);
        // Window class lives with the pinned DLL; no per-attach class race.
        attaching=false;
    }
    static LRESULT CALLBACK WindowProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) noexcept {
        auto self=reinterpret_cast<Integration*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            self=static_cast<Integration*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->AddRef();
            self->control=hwnd;
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(self && message==WM_NCDESTROY) {
            self->Remove();
            RemovePropW(hwnd,DH_TASKBAR_CURRENT); RemovePropW(hwnd,DH_TASKBAR_OWNER);
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);
            self->control=nullptr; attaching=false;
            self->Release();
            return DefWindowProcW(hwnd,message,wp,lp);
        }
        if(self && (message==RemoveButton || message==OwnerExited)) { self->Close();return 1; }
        try {
            if(self && message==self->state) {
                if(wp<=DH_TASKBAR_OTHER_DESKTOP && reinterpret_cast<HWND>(lp)==self->helper &&
                   self->OwnerAlive() && self->active!=wp) {
                    self->active=static_cast<unsigned>(wp);self->Refresh();
                }
                return 0;
            }
            if(self && message==InspectButton)return self->Inspect();
        } catch(...) { self->RecordError(to_hresult());self->Close();return 0; }
        return DefWindowProcW(hwnd,message,wp,lp);
    }
    void Mount(Grid grid) {
        Checkpoint(1);
        if(grid.ColumnDefinitions().Size()!=0)throw hresult_error(E_NOTIMPL,L"Unexpected root-grid columns");
        parent=grid;
        bool repeaterFound=false;
        for(auto child:parent.Children())if(auto fe=child.try_as<FrameworkElement>()) {
            placements.push_back({fe,Grid::GetColumn(fe),Grid::GetColumnSpan(fe)});
            if(fe.Name()==L"TaskbarFrameRepeater")repeaterFound=true;
        }
        if(!repeaterFound)throw hresult_error(E_NOTIMPL,L"Taskbar repeater missing");
        strip=StackPanel();
        strip.Name(L"DesktopsHelperNativeStrip");
        strip.Orientation(Orientation::Horizontal);
        strip.Margin(Thickness{8,0,8,0});strip.VerticalAlignment(VerticalAlignment::Center);
        // Keep native toggle semantics and hit targets, but let the taskbar show
        // through. Theme resources adapt the ink and hover surface automatically.
        auto digitTemplate=Markup::XamlReader::Load(LR"(
<ControlTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                 xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType="ToggleButton">
  <Grid Background="Transparent">
    <VisualStateManager.VisualStateGroups>
      <VisualStateGroup x:Name="CommonStates">
        <VisualState x:Name="Normal"/>
        <VisualState x:Name="PointerOver">
          <VisualState.Setters><Setter Target="HoverSurface.Opacity" Value="0.07"/></VisualState.Setters>
        </VisualState>
        <VisualState x:Name="Pressed">
          <VisualState.Setters><Setter Target="HoverSurface.Opacity" Value="0.12"/></VisualState.Setters>
        </VisualState>
        <VisualState x:Name="Disabled">
          <VisualState.Setters><Setter Target="ContentPresenter.Opacity" Value="0.4"/></VisualState.Setters>
        </VisualState>
      </VisualStateGroup>
      <VisualStateGroup x:Name="CheckStates">
        <VisualState x:Name="Unchecked"/><VisualState x:Name="Checked"/><VisualState x:Name="Indeterminate"/>
      </VisualStateGroup>
    </VisualStateManager.VisualStateGroups>
    <Border x:Name="HoverSurface" Margin="1,3" CornerRadius="4" Opacity="0"
            Background="{ThemeResource SystemControlForegroundBaseHighBrush}"/>
    <ContentPresenter x:Name="ContentPresenter" Content="{TemplateBinding Content}"
                      Foreground="{ThemeResource SystemControlForegroundBaseHighBrush}"
                      FontWeight="{TemplateBinding FontWeight}"
                      HorizontalAlignment="Center" VerticalAlignment="Center"/>
  </Grid>
</ControlTemplate>)").as<ControlTemplate>();
        for(unsigned i=0;i<buttons.size();i++) {
            auto button=Primitives::ToggleButton();buttons[i]=button;
            wchar_t label[2]={static_cast<wchar_t>(L'1'+i),0};
            labels[i]=TextBlock();labels[i].Text(label);
            labels[i].FontFamily(Media::FontFamily(L"Cascadia Mono, Consolas"));
            labels[i].FontSize(12);
            button.Content(labels[i]);button.Template(digitTemplate);
            button.Width(24);button.MinWidth(0);button.Height(32);button.MinHeight(0);
            button.Padding(Thickness{0,0,0,0});button.Margin(Thickness{0,0,0,0});
            button.IsTabStop(false);button.AllowFocusOnInteraction(false);
            wchar_t name[64];swprintf_s(name,L"Switch to desktop %u",i+1);
            Automation::AutomationProperties::SetName(button,name);
            ToolTipService::SetToolTip(button,box_value(name));
            clicks[i]=button.Click([weak=get_weak(),i](auto const&,auto const&) noexcept {
                if(auto self=weak.get()) {
                    try {
                        if(!self->OwnerAlive()){self->Close();return;}
                        self->Request(i+1);
                        // Undo optimistic ToggleButton state until service confirmation.
                        self->Refresh();
                    } catch(...) { self->RecordError(to_hresult());self->Close(); }
                }
            });
            subscribed[i]=true;
            strip.Children().Append(button);Checkpoint(2+i);
        }
        leftColumn=ColumnDefinition();leftColumn.Width(GridLength{1,GridUnitType::Auto});
        contentColumn=ColumnDefinition();contentColumn.Width(GridLength{1,GridUnitType::Star});
        parent.ColumnDefinitions().Append(leftColumn);Checkpoint(7);
        parent.ColumnDefinitions().Append(contentColumn);Checkpoint(8);
        for(auto const& entry:placements) {
            if(entry.element.Name()==L"TaskbarFrameRepeater")Grid::SetColumn(entry.element,1);
            else {Grid::SetColumn(entry.element,0);Grid::SetColumnSpan(entry.element,2);}
        }
        Checkpoint(9);
        Grid::SetColumn(strip,0);parent.Children().Append(strip);Checkpoint(10);
        Refresh();
        if(!RegisterWaitForSingleObject(&ownerWait,ownerProcess,OnOwnerExit,this,INFINITE,WT_EXECUTEONLYONCE))
            throw hresult_error(HRESULT_FROM_WIN32(GetLastError()));
        Checkpoint(11);
        if(!SetPropW(control,DH_TASKBAR_OWNER,reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(helperPid))) ||
           !SetPropW(control,DH_TASKBAR_MOUNTED,reinterpret_cast<HANDLE>(1)))
            throw hresult_error(HRESULT_FROM_WIN32(GetLastError()));
        mounted=true;Request(DH_TASKBAR_SUBSCRIBE);
        CloseHandle(controllerProcess);controllerProcess=nullptr;
    }
    void Attach(HWND taskbar,HWND owner,DWORD controllerPid) {
        helper=owner;
        wchar_t name[96]{};
        if(!GetClassNameW(helper,name,96) || wcscmp(name,DH_HELPER_CLASS) ||
           !GetWindowThreadProcessId(helper,&helperPid))throw hresult_error(E_INVALIDARG);
        ownerProcess=OpenProcess(SYNCHRONIZE,FALSE,helperPid);
        controllerProcess=OpenProcess(SYNCHRONIZE,FALSE,controllerPid);
        deadline=GetTickCount64()+AttachTimeoutMs;
        CheckAttachment();RemovePropW(helper,DH_TASKBAR_ERROR);
        auto root=NativeRoot(taskbar);
        std::vector<DependencyObject> pending{root};Grid target{nullptr};
        for(unsigned count=0;!pending.empty() && count<4096;count++) {
            auto item=pending.back();pending.pop_back();
            if(auto fe=item.try_as<FrameworkElement>();fe && fe.Name()==L"RootGrid") {
                auto ownerElement=Media::VisualTreeHelper::GetParent(fe).try_as<FrameworkElement>();
                if(ownerElement && get_class_name(ownerElement)==L"Taskbar.TaskbarFrame") {
                    target=fe.try_as<Grid>();break;
                }
            }
            int n=Media::VisualTreeHelper::GetChildrenCount(item);
            for(int i=0;i<n;i++)pending.push_back(Media::VisualTreeHelper::GetChild(item,i));
        }
        if(!target)throw hresult_error(E_FAIL,L"Taskbar content grid not found");
#ifdef DH_TASKBAR_TESTING
        // Exercise an expired queued request without blocking Explorer's thread.
        if(reinterpret_cast<UINT_PTR>(GetPropW(helper,L"DesktopsHelper.TestFailStage"))==12)deadline=0;
#endif
        auto lifetime=get_strong();
        target.Dispatcher().RunAsync(Windows::UI::Core::CoreDispatcherPriority::Low,[lifetime,target]() noexcept {
            try {
                lifetime->CheckAttachment();
                WNDCLASSW wc{};
                wc.hInstance=module;wc.lpszClassName=ControlClass;wc.lpfnWndProc=WindowProc;
                if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
                    throw hresult_error(HRESULT_FROM_WIN32(GetLastError()));
                if(!CreateWindowExW(0,ControlClass,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,module,lifetime.get()))
                    throw hresult_error(HRESULT_FROM_WIN32(GetLastError()));
                lifetime->Mount(target);
            } catch(...) { lifetime->RecordError(to_hresult());lifetime->Close(); }
        });
    }
};
extern "C" LRESULT CALLBACK BootstrapHook(int code,WPARAM wp,LPARAM lp) noexcept {
    if(code==HC_ACTION) {
        auto message=reinterpret_cast<CWPSTRUCT*>(lp);
        if(message->message==RegisterWindowMessageW(AttachMessage) && message->wParam && message->lParam) {
            wchar_t cls[64]{};GetClassNameW(message->hwnd,cls,64);
            DWORD owner=0;GetWindowThreadProcessId(message->hwnd,&owner);
            if(!wcscmp(cls,L"Shell_TrayWnd") && owner==GetCurrentProcessId() && !attaching.exchange(true)) {
                HMODULE pinned;
                if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                                      reinterpret_cast<LPCWSTR>(&module),&pinned)) {
                    attaching=false;return CallNextHookEx(nullptr,code,wp,lp);
                }
                try {
                    auto integration=make_self<Integration>();
                    try {
                        integration->Attach(message->hwnd,reinterpret_cast<HWND>(message->wParam),
                                            static_cast<DWORD>(message->lParam));
                    } catch(...) {integration->RecordError(to_hresult());integration->Close();}
                } catch(...) {attaching=false;}
            }
        }
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){module=instance;DisableThreadLibraryCalls(instance);}
    return TRUE;
}
