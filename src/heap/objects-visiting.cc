// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/objects-visiting.h"

#include "src/heap/heap-inl.h"
#include "src/heap/mark-compact-inl.h"
#include "src/heap/objects-visiting-inl.h"

namespace v8 {
namespace internal {

// We don't record weak slots during marking or scavenges. Instead we do it
// once when we complete mark-compact cycle.  Note that write barrier has no
// effect if we are already in the middle of compacting mark-sweep cycle and we
// have to record slots manually.
static bool MustRecordSlots(Heap* heap) {
  return heap->gc_state() == Heap::MARK_COMPACT &&
         heap->mark_compact_collector()->is_compacting();
}


template <class T>
struct WeakListVisitor;


template <class T>
Object* VisitWeakList(Heap* heap, Object* list, WeakObjectRetainer* retainer) {
  Object* undefined = heap->undefined_value();
  Object* head = undefined;
  T* tail = nullptr;
  bool record_slots = MustRecordSlots(heap);

  while (list != undefined) {
    // Check whether to keep the candidate in the list.
    T* candidate = reinterpret_cast<T*>(list);

    Object* retained = retainer->RetainAs(list);
    if (retained != nullptr) {
      if (head == undefined) {
        // First element in the list.
        head = retained;
      } else {
        // Subsequent elements in the list.
        DCHECK_NOT_NULL(tail);
        WeakListVisitor<T>::SetWeakNext(tail, retained);
        if (record_slots) {
          HeapObject* slot_holder = WeakListVisitor<T>::WeakNextHolder(tail);
          int slot_offset = WeakListVisitor<T>::WeakNextOffset();
          Object** slot = HeapObject::RawField(slot_holder, slot_offset);
          MarkCompactCollector::RecordSlot(slot_holder, slot, retained);
        }
      }
      // Retained object is new tail.
      DCHECK(!retained->IsUndefined(heap->isolate()));
      candidate = reinterpret_cast<T*>(retained);
      tail = candidate;

      // tail is a live object, visit it.
      WeakListVisitor<T>::VisitLiveObject(heap, tail, retainer);

    } else {
      WeakListVisitor<T>::VisitPhantomObject(heap, candidate);
    }

    // Move to next element in the list.
    list = WeakListVisitor<T>::WeakNext(candidate);
  }

  // Terminate the list if there is one or more elements.
  if (tail != nullptr) WeakListVisitor<T>::SetWeakNext(tail, undefined);
  return head;
}


template <class T>
static void ClearWeakList(Heap* heap, Object* list) {
  Object* undefined = heap->undefined_value();
  while (list != undefined) {
    T* candidate = reinterpret_cast<T*>(list);
    list = WeakListVisitor<T>::WeakNext(candidate);
    WeakListVisitor<T>::SetWeakNext(candidate, undefined);
  }
}

template <>
struct WeakListVisitor<Code> {
  static void SetWeakNext(Code* code, Object* next) {
    code->code_data_container()->set_next_code_link(next,
                                                    UPDATE_WEAK_WRITE_BARRIER);
  }

  static Object* WeakNext(Code* code) {
    return code->code_data_container()->next_code_link();
  }

  static HeapObject* WeakNextHolder(Code* code) {
    return code->code_data_container();
  }

  static int WeakNextOffset() { return CodeDataContainer::kNextCodeLinkOffset; }

  static void VisitLiveObject(Heap*, Code*, WeakObjectRetainer*) {}

  static void VisitPhantomObject(Heap*, Code*) {}
};


template <>
struct WeakListVisitor<Context> {
  static void SetWeakNext(Context* context, Object* next) {
    context->set(Context::NEXT_CONTEXT_LINK, next, UPDATE_WEAK_WRITE_BARRIER);
  }

  static Object* WeakNext(Context* context) {
    return context->next_context_link();
  }

  static HeapObject* WeakNextHolder(Context* context) { return context; }

  static int WeakNextOffset() {
    return FixedArray::SizeFor(Context::NEXT_CONTEXT_LINK);
  }

  static void VisitLiveObject(Heap* heap, Context* context,
                              WeakObjectRetainer* retainer) {
    if (heap->gc_state() == Heap::MARK_COMPACT) {
      // Record the slots of the weak entries in the native context.
      for (int idx = Context::FIRST_WEAK_SLOT;
           idx < Context::NATIVE_CONTEXT_SLOTS; ++idx) {
        Object** slot = Context::cast(context)->RawFieldOfElementAt(idx);
        MarkCompactCollector::RecordSlot(context, slot, *slot);
      }
      // Code objects are always allocated in Code space, we do not have to
      // visit them during scavenges.
      DoWeakList<Code>(heap, context, retainer, Context::OPTIMIZED_CODE_LIST);
      DoWeakList<Code>(heap, context, retainer, Context::DEOPTIMIZED_CODE_LIST);
    }
  }

  template <class T>
  static void DoWeakList(Heap* heap, Context* context,
                         WeakObjectRetainer* retainer, int index) {
    // Visit the weak list, removing dead intermediate elements.
    Object* list_head = VisitWeakList<T>(heap, context->get(index), retainer);

    // Update the list head.
    context->set(index, list_head, UPDATE_WRITE_BARRIER);

    if (MustRecordSlots(heap)) {
      // Record the updated slot if necessary.
      Object** head_slot =
          HeapObject::RawField(context, FixedArray::SizeFor(index));
      heap->mark_compact_collector()->RecordSlot(context, head_slot, list_head);
    }
  }

  static void VisitPhantomObject(Heap* heap, Context* context) {
    ClearWeakList<Code>(heap, context->get(Context::OPTIMIZED_CODE_LIST));
    ClearWeakList<Code>(heap, context->get(Context::DEOPTIMIZED_CODE_LIST));
  }
};


template <>
struct WeakListVisitor<AllocationSite> {
  static void SetWeakNext(AllocationSite* obj, Object* next) {
    obj->set_weak_next(next, UPDATE_WEAK_WRITE_BARRIER);
  }

  static Object* WeakNext(AllocationSite* obj) { return obj->weak_next(); }

  static HeapObject* WeakNextHolder(AllocationSite* obj) { return obj; }

  static int WeakNextOffset() { return AllocationSite::kWeakNextOffset; }

  static void VisitLiveObject(Heap*, AllocationSite*, WeakObjectRetainer*) {}

  static void VisitPhantomObject(Heap*, AllocationSite*) {}
};


template Object* VisitWeakList<Context>(Heap* heap, Object* list,
                                        WeakObjectRetainer* retainer);

template Object* VisitWeakList<AllocationSite>(Heap* heap, Object* list,
                                               WeakObjectRetainer* retainer);
}  // namespace internal
}  // namespace v8
