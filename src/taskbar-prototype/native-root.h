#pragma once
// Research-only profile, resolved from Microsoft's PDB for this exact image.
// Any different image is rejected before following private pointers.
static bool Readable(const void* pointer,size_t length) {
    MEMORY_BASIC_INFORMATION region{};
    if(!pointer || !VirtualQuery(pointer,&region,sizeof(region)) || region.State!=MEM_COMMIT ||
       (region.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    auto start=reinterpret_cast<uintptr_t>(pointer),base=reinterpret_cast<uintptr_t>(region.BaseAddress);
    return start>=base && length<=region.RegionSize && start-base<=region.RegionSize-length;
}
static FrameworkElement NativeRoot(HWND taskbar) {
    auto image=reinterpret_cast<const BYTE*>(GetModuleHandleW(L"Taskbar.dll"));
    if(!image)throw hresult_error(E_FAIL,L"Taskbar module missing");
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    auto pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
    if(pe->FileHeader.TimeDateStamp!=2378140881u || pe->OptionalHeader.SizeOfImage!=3162112u)
        throw hresult_error(E_NOTIMPL,L"Unqualified Taskbar.dll image");
    constexpr BYTE framePrefix[]={0x48,0x83,0xec,0x28,0x48,0x83,0xc1,0x10};
    if(memcmp(image+0x8aefc,framePrefix,sizeof(framePrefix)))throw hresult_error(E_NOTIMPL,L"Taskbar host layout mismatch");
    HWND band=reinterpret_cast<HWND>(GetPropW(taskbar,L"TaskbandHWND"));
    auto object=reinterpret_cast<void**>(GetWindowLongPtrW(band,0));
    if(!Readable(object,20*sizeof(void*)))throw hresult_error(E_FAIL,L"Task band unavailable");
    void* site=nullptr;
    for(unsigned slot=0;slot<20;slot++)if(object[slot]==image+0x1cbf50){site=object+slot;break;}
    if(!site)throw hresult_error(E_NOTIMPL,L"Task band interface not found");
    struct HostReference {
        void* object=nullptr;void* ownership=nullptr;
        const BYTE* image=nullptr;
        ~HostReference(){if(ownership)reinterpret_cast<void(*)(void*)>(const_cast<BYTE*>(image)+0x27ca0)(ownership);}
    } host;
    host.image=image;
    using GetHost=void*(*)(void*,void*);
    reinterpret_cast<GetHost>(const_cast<BYTE*>(image)+0x1097a0)(site,&host);
    if(!host.ownership || !Readable(host.object,24))throw hresult_error(E_FAIL,L"Taskbar host unavailable");
    auto element=*reinterpret_cast<IUnknown**>(static_cast<BYTE*>(host.object)+16);
    if(!Readable(element,sizeof(void*)))throw hresult_error(E_FAIL,L"Taskbar element unavailable");
    FrameworkElement frame{nullptr};check_hresult(element->QueryInterface(guid_of<FrameworkElement>(),put_abi(frame)));
    return frame.XamlRoot().Content().as<FrameworkElement>();
}
