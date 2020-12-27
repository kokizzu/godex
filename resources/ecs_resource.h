#pragma once

/* Author: AndreaCatania */

#include "../ecs_types.h"
#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/templates/local_vector.h"
#include "core/templates/oa_hash_map.h"

#define RESOURCE(m_class)                                                   \
	ECSCLASS(m_class)                                                       \
																			\
private:                                                                    \
	/* Creation */                                                          \
	static _FORCE_INLINE_ m_class *create_resource() {                      \
		return memnew(m_class);                                             \
	}                                                                       \
	static _FORCE_INLINE_ godex::Resource *create_resource_no_type() {      \
		/* Creates a storage but returns a generic component. */            \
		return create_resource();                                           \
	}                                                                       \
																			\
	/* Resource */                                                          \
	static inline uint32_t resource_id = UINT32_MAX;                        \
																			\
public:                                                                     \
	static uint32_t get_resource_id() { return resource_id; }               \
	virtual godex::resource_id rid() const override { return resource_id; } \
																			\
	ECS_PROPERTY_MAPPER(m_class)                                            \
private:

namespace godex {

class Resource : public ECSClass {
	ECSCLASS(Resource)

public:
	Resource();

public:
	static void _bind_properties();

	/// Returns the resouce ID.
	virtual resource_id rid() const;

	virtual const LocalVector<PropertyInfo> *get_properties() const;
	virtual bool set(const StringName &p_name, const Variant &p_data);
	virtual bool get(const StringName &p_name, Variant &r_data) const;

	virtual bool set(const uint32_t p_parameter_index, const Variant &p_data);
	virtual bool get(const uint32_t p_parameter_index, Variant &r_data) const;

	Variant get(const StringName &p_name) const;
};

} // namespace godex

/// This class is used to give `Resource` access from GDScript and make sure the
/// mutability is respected.
class AccessResource : public Object {
public:
	godex::Resource *__resource = nullptr;
	bool __mut = false;

	AccessResource();

	bool _setv(const StringName &p_name, const Variant &p_data);
	bool _getv(const StringName &p_name, Variant &r_data) const;

	bool is_mutable() const;

public:
	template <class T>
	static T *unwrap(Object *p_access_resource);

	template <class T>
	static const T *unwrap(const Object *p_access_resource);
};

template <class T>
T *AccessResource::unwrap(Object *p_access_resource) {
	AccessResource *res = Object::cast_to<AccessResource>(p_access_resource);
	if (unlikely(res == nullptr)) {
		return nullptr;
	}

	ERR_FAIL_COND_V_MSG(res->__mut == false, nullptr, "This is an immutable resource.");
	if (likely(res->__resource->rid() == T::get_resource_id())) {
		return static_cast<T *>(res->__resource);
	} else {
		return nullptr;
	}
}

template <class T>
const T *AccessResource::unwrap(const Object *p_access_resource) {
	const AccessResource *res = Object::cast_to<AccessResource>(p_access_resource);
	if (unlikely(res == nullptr)) {
		return nullptr;
	}
	if (likely(res->__resource->rid() == T::get_resource_id())) {
		return static_cast<const T *>(res->__resource);
	} else {
		return nullptr;
	}
}
