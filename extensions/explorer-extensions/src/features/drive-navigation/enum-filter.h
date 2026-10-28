#pragma once
#include <atomic>
#include <cstdint>
#include <new>
#include <string_view>

#include <windows.h>
#include <shlobj.h>
#include "drive-path.h"

namespace DriveNavFilter {

std::atomic<bool> enabled{false};

std::uint32_t ReadHiddenDriveMask();
bool ShouldHide(PCUITEMID_CHILD child, std::uint32_t mask);

// The native enumerator stays intact. No Shell object, filesystem item or
// registry policy is deleted. Only the navigation caller sees the filtered view.
class Enumerator final : public IEnumIDList {
    std::atomic<ULONG> references_{1};
    IEnumIDList* inner_;
    std::uint32_t mask_;

    ~Enumerator() { inner_->Release(); }

public:
    // Takes ownership of the reference returned by EnumObjects/Clone.
    Enumerator(IEnumIDList* inner, std::uint32_t mask) : inner_(inner), mask_(mask) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (!IsEqualIID(iid, IID_IUnknown) && !IsEqualIID(iid, IID_IEnumIDList)) {
            return E_NOINTERFACE;
        }
        *result = static_cast<IEnumIDList*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE Next(ULONG requested, PITEMID_CHILD* items,
                                   ULONG* fetched) override {
        if (fetched) *fetched = 0;
        if (!items || (!fetched && requested != 1)) return E_POINTER;
        ULONG count = 0;
        HRESULT failure = S_OK;
        while (count < requested) {
            PITEMID_CHILD item = nullptr;
            ULONG received = 0;
            HRESULT hr = inner_->Next(1, &item, &received);
            if (FAILED(hr)) {
                // On error the native contract says no output item is valid.
                failure = hr;
                break;
            }
            if (received == 0) {
                // Avoid spinning even if a native implementation returns S_OK
                // with no item. A non-null unused output still belongs to us.
                CoTaskMemFree(item);
                break;
            }
            if (received != 1 || !item) {
                CoTaskMemFree(item);
                failure = E_UNEXPECTED;
                break;
            }
            if (enabled.load() && ShouldHide(item, mask_)) {
                CoTaskMemFree(item);
            } else {
                items[count++] = item;
            }
            if (hr == S_FALSE) break;
        }
        if (FAILED(failure)) {
            // Next's error contract forbids returning partially valid output.
            for (ULONG i = 0; i < count; ++i) {
                CoTaskMemFree(items[i]);
                items[i] = nullptr;
            }
            return failure;
        }
        if (fetched) *fetched = count;
        return count == requested ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG requested) override {
        // Native Skip counts hidden entries, so consume the filtered sequence.
        for (ULONG i = 0; i < requested; ++i) {
            PITEMID_CHILD item = nullptr;
            HRESULT hr = Next(1, &item, nullptr);
            CoTaskMemFree(item);
            if (hr != S_OK) return hr;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Reset() override {
        HRESULT hr = inner_->Reset();
        if (SUCCEEDED(hr)) mask_ = enabled.load() ? ReadHiddenDriveMask() : 0;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Clone(IEnumIDList** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        IEnumIDList* copy = nullptr;
        HRESULT hr = inner_->Clone(&copy);
        if (FAILED(hr)) return hr;
        if (!copy) return E_UNEXPECTED;
        auto wrapped = new (std::nothrow) Enumerator(copy, mask_);
        if (!wrapped) { copy->Release(); return E_OUTOFMEMORY; }
        *result = wrapped;
        return hr;
    }
};

} // namespace DriveNavFilter
