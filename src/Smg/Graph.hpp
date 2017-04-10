#pragma once

#include "Smg_fwd.hpp"
#include "Edge.hpp"
#include "Object.hpp"

#include "../Definitions.hpp"
#include "../Exceptions.hpp"
#include "../Type.hpp"
#include "../Values.hpp"

//#include <gsl/gsl_algorithm>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <string>
#include <sstream>

#include <iostream>

#include <tuple>

//#include <gsl/gsl>
#include <range/v3/all.hpp>

class InvalidDereferenceException_smg : public std::logic_error {
public:
  InvalidDereferenceException_smg() : std::logic_error("") {}
};

namespace Smg {
namespace Impl {

class Graph {
private:

public:
  Object handles;
  std::map<ObjectId, uptr<Object>> objects;

public:

  Graph(IValueContainer* vc) :
    vc(vc)
  {
    auto& obj = objects.emplace(ObjectId{0}, std::make_unique<Object>()).first.operator*().second.operator*();
    obj.id = ObjectId{0};
    obj.size = ValueId{0};
    handles.CreatePtEdge(PtEdge{ValueId{0}, ValueId{0}, Type::CreateCharPointerType(), ObjectId{0}, GetVc().GetZero(PTR_TYPE)});
  }
  Graph(const Graph& g) :
    handles(g.handles),
    vc(g.vc)
  {
    //objects = ranges::transform(
    //  g.objects, 
    //  [](const decltype(objects)::value_type& kvp) { 
    //    return decltype(objects)::value_type{kvp.first, std::make_unique<Object>(*kvp.second)}; 
    //    }
    //  );
    for (const auto& kvp : g.objects)
    {
      objects.emplace(kvp.first, std::make_unique<Object>(*kvp.second));
    }
  }


  IValueContainer* vc;
  //gsl::not_null<IValueContainer*> vc;
  IValueContainer& GetVc() { return *vc; }

  // Returns PtEdge [object, offset, type] corresponding to given pointer value
  // The given pointer must be bound to an existing object, otherwise it is an undefined behaviour!
  const PtEdge& FindPtEdge(ValueId ptr)
  {
    // The given pointer must be bound to an existing object
    auto objectHandle = handles.FindPtEdgeByValue(ptr);
    assert(objectHandle != nullptr); //TODO: maybe an exception?
    return *objectHandle;
  }

  // Returns PtEdge [object, offset, type] corresponding to given pointer value and type
  // The given pointer must be bound to an existing object, otherwise it is an undefined behaviour!
  const PtEdge& FindPtEdge(ValueId ptr, Type type)
  {
    // The given pointer must be bound to an existing object
    auto objectHandle = handles.FindPtEdgeByValueType(ptr, type);
    assert(objectHandle != nullptr); //TODO: maybe an exception?
    return *objectHandle;
  }

  // Returns new pointer to different field [baseOffset + offset, type] of the same object
  auto CreateDerivedPointer(ValueId basePtr, ValueId offset, Type type)
  {
    auto& baseEdge = FindPtEdge(basePtr);
    auto derivedOffset = GetVc().Add(baseEdge.targetOffset, offset, PTR_TYPE, ArithFlags::Default);
    auto derivedValue = GetVc().Add(basePtr, derivedOffset, PTR_TYPE, ArithFlags::Default);
    auto& derEdge = handles.CreatePtEdge(PtEdge{baseEdge, derivedValue, type, derivedOffset});
    //std::vector<int>().em
    return std::make_pair<decltype(derivedValue), ref_wr<std::decay<decltype(derEdge)>::type>>(std::move(derivedValue), derEdge);
  }

  enum class MemorySpace : int8_t {
    Static,
    ThreadLocal,
    Stack,
    Heap
  };

  // Returns a pointer to newly allocated object
  ValueId AllocateObject(Type type, MemorySpace ms = MemorySpace::Heap)
  {
    // assign a new ObjectId and new ValueId representing the resultant pointer
    //TODO: different acquisition of ValueId based on MemorySpace

    auto ptrToTypeT = Type::CreatePointerTo(type);

    ObjectId oid = ObjectId::GetNextId();
    ValueId  ptr = GetVc().CreateVal(ptrToTypeT);//ValueId ::GetNextId();

                                                 // move from new uptr<Object>
                                                 // create new object and place it into map
    auto& obj = objects.emplace(oid, std::make_unique<Object>()).first.operator*().second.operator*();

    // initialize the object
    obj.id = oid;
    obj.size = GetVc().CreateConstIntVal(type.GetSizeOf());

    handles.CreatePtEdge(PtEdge{ValueId{0}, ptr, ptrToTypeT, oid, GetVc().GetZero(PTR_TYPE)});

    return ptr;
  }

  //template<typename UniqueOrderedMap, typename ModifiedValueType>
  //ValueId CreateOrModifyManual(
  //  UniqueOrderedMap& map,
  //  UniqueOrderedMap::key_type&& key,
  //  UniqueOrderedMap::mapped_type&& newValue
  //  ModifiedValueType&& value,
  //  ModifiedValueType* accessor = &UniqueOrderedMap::key_type
  //  )
  //{
  //  // src: http://stackoverflow.com/a/101980

  //  decltype(map)::iterator lb = map.lower_bound(arg.id);

  //  if (lb != map.end() && !(map.key_comp()(arg.id, lb->first)))
  //  {
  //    // key already exists
  //    // update lb->second if you care to
  //    lb->second::*accessor = std::forward<ModifiedValueType&&>(value);
  //    return lb->second;
  //  }
  //  else
  //  {
  //    // the key does not exist in the innerMap
  //    // add it to the innerMap, use hint
  //    map.insert(lb, arg.id, std::forward<mapped_type&&>(newValue));
  //    return newValue;
  //  }
  //}

  ValueId ReadValue(ValueId ptr, Type ptrType, Type tarType)
  {
    auto& ptrEdge = FindPtEdge(ptr, ptrType);

    auto  objectId = ptrEdge.targetObjectId;
    auto& object = *objects.at(objectId);
    auto  offset = ptrEdge.targetOffset;

    if (!tarType.IsPointer()) // type is just a value, so it is just a plain HV edge
    {
      if (HvEdge* edge = object.FindHvEdgeByOffset(offset /*, tarType */)) //TODO: we should search by type too!
      { // HvEdge already exists        
        return (*edge).value;
      }
      else
      { // HvEdges does not yet exist -> we should attempt read reinterpretation, if there are any values
        //TODO: should newly allocated object be covered by unknown value of object's size,
        // or there just should not be any HvEdges at all?
        if (object.hvEdges.size() == 0)
        { // Unknown!
          throw InvalidDereferenceException_smg();
        }
        //TODO: read reinterpretation
        throw NotSupportedException(
          "HvEdges for such offset does not yet exists and read re-interpretation is not yet supported"
        );
      }
    }
    else /* type is pointer && and is known pointer; debug if second fails */
    {
      if (PtEdge* edge = object.FindPtEdgeByOffset(offset))
      {
        handles.CreatePtEdge(PtEdge{ValueId{0},*edge});
        return (*edge).value;
      }
      else
      {
        // Throw!
        // Or fallback to HvEdge scenario
        // Or use undefined value / special meaning value
        // Find out whether it is undefined-unknown or abstracted-unknown
        auto status = GetVc().GetAbstractionStatus(ptr);
        std::cout << AbstractionStatusToString(status) << std::endl;
        throw std::runtime_error{"Reading unknown pointer value"}; //HACK!, need this for argv 
      }
    }

  }

  // Type is potentially not needed, because the targetPtr must be a pointer to correctly typed edge
  // We use it here to skip the ptrEdge.valueType.GetPointerElementType()
  void WriteValue(ValueId ptr, ValueId value, Type type)
  {
    auto& ptrEdge = FindPtEdge(ptr, Type::CreatePointerTo(type));

    WriteValue(ptrEdge, value, type);
  }
  void WriteValue(const PtEdge& ptrEdge, ValueId value, Type type)
  {
    // The pointer target type and the type of value being written must not differ
    assert(ptrEdge.valueType.GetPointerElementType() == type);

    // The object has to be valid for an operation
    // The flag should be stored inside the object, because the edge is unique to [object, offset, type]
    auto  objectId = ptrEdge.targetObjectId;
    auto& object = *objects.at(objectId);
    auto  offset = ptrEdge.targetOffset;

    if (!type.IsPointer()) // type is just a value, so it is just a plain HV edge
    {
      object.CreateOrModifyHvEdge(offset, value, type);

      //// Repeat the same logic - does the edge exists? -> modify or create
      //HvEdge* edge;
      //if (edge = object.FindHvEdge(offset))
      //{ // Then modify it
      //  //TODO: edge modification -> should have a reference counter, when the original value is no longer accessible - FOR VALUES? PROBABLY NO
      //  edge->value = value;
      //}
      //else
      //{ // Create new edge
      //  object.CreateHvEdge(HvEdge{offset, value, type});
      //}

    }
    else /* type is pointer && and is known pointer; debug if second fails */
    {
      if (PtEdge* assignedPtr = handles.FindPtEdgeByValue(value))
      {
        object.CreateOrModifyPtEdge(offset, *assignedPtr);
      }
      else
      {
        // Throw!
        // Or fallback to HvEdge scenario
        // Or use undefined value / special meaning value
        // Find out whether it is undefined-unknown or abstracted-unknown
        auto status = GetVc().GetAbstractionStatus(value);
        std::cout << AbstractionStatusToString(status) << std::endl;
        //HACK: need this for argv 
        ////throw std::runtime_error{"Writing unknown pointer value"}; 
      }
    }

  }

};

} // namespace Smg::Impl
} // namespace Smg
