#include "pipeline.h"

#include "../ecs.h"
#include "../storage/hierarchical_storage.h"
#include "../world/world.h"

Pipeline::Pipeline() {
}

void Pipeline::set_is_sub_dispatcher(bool p_sub_dispatcher) {
	is_sub_dispatcher = p_sub_dispatcher;
}

bool Pipeline::get_is_sub_dispatcher() const {
	return is_sub_dispatcher;
}

// Unset the macro defined into the `pipeline.h` so to properly point the method
// definition.
#undef add_temporary_system
void Pipeline::add_temporary_system(func_temporary_system_execute p_func_get_exe_info) {
	temporary_systems_exe.push_back(p_func_get_exe_info);
}

void Pipeline::add_registered_temporary_system(godex::system_id p_id) {
	add_temporary_system(ECS::get_func_temporary_system_exe(p_id));
}

// Unset the macro defined into the `pipeline.h` so to properly point the method
// definition.
#undef add_system
uint32_t Pipeline::add_system(func_get_system_exe_info p_func_get_exe_info) {
#ifdef DEBUG_ENABLED
	// This is automated by the `add_system` macro or by
	// `ECS::register_system` macro, so is never supposed to happen.
	CRASH_COND_MSG(p_func_get_exe_info == nullptr, "The passed system constructor can't be nullptr at this point.");
	// Using crash cond because pipeline composition is not directly exposed
	// to the user.
	CRASH_COND_MSG(is_ready(), "The pipeline is ready, you can't modify it.");
#endif
	const uint32_t in_pipeline_id = systems_info.size();
	systems_info.push_back(p_func_get_exe_info);
	return in_pipeline_id;
}

uint32_t Pipeline::add_registered_system(godex::system_id p_id) {
	const uint32_t in_pipeline_id = add_system(ECS::get_func_system_exe_info(p_id));
	if (ECS::is_system_dispatcher(p_id)) {
		system_dispatchers.push_back(in_pipeline_id);
	}
	return in_pipeline_id;
}

void Pipeline::build() {
#ifdef DEBUG_ENABLED
	CRASH_COND_MSG(ready, "You can't build a pipeline twice.");
#endif
	ready = true;

	systems_exe.reserve(systems_info.size());

	SystemExeInfo info;
	for (uint32_t i = 0; i < systems_info.size(); i += 1) {
		info.clear();
		systems_info[i](info);

		ERR_CONTINUE_MSG(info.valid == false, "[FATAL][FATAL][FATAL][PIPELINE-FATAL] The system with index: " + itos(i) + " is invalid. Excluded from pipeline.");

#ifdef DEBUG_ENABLED
		// This is automated by the `add_system` macro or by
		// `ECS::register_system` macro, so is never supposed to happen.
		CRASH_COND_MSG(info.system_func == nullptr, "At this point `info.system_func` is supposed to be not null. To add a system use the following syntax: `add_system(function_name);` or use the `ECS` class to get the `SystemExeInfo` if it's a registered system.");
#endif

		systems_exe.resize(systems_exe.size() + 1);
		ExecutionData &ed = systems_exe[systems_exe.size() - 1];

		ed.exe = info.system_func;

		const bool is_system_dispatcher = system_dispatchers.find(i) != -1;

		if (is_system_dispatcher == false) {
			// Take the events that are generated by this pipeline
			// (no sub pipelines).
			for (const Set<uint32_t>::Element *e = info.mutable_components_storage.front(); e; e = e->next()) {
				if (ECS::is_component_events(e->get())) {
					// Make sure it's unique
					if (event_generator.find(e->get()) == -1) {
						event_generator.push_back(e->get());
					}
				}
			}

			// Mark as flush, the storages that need to be flushed at the end
			// of the `System`.
			ed.notify_list_release_write.clear();
			for (const Set<uint32_t>::Element *e = info.mutable_components.front(); e; e = e->next()) {
				if (ECS::storage_notify_release_write(e->get())) {
					ed.notify_list_release_write.push_back(e->get());
				}
			}
			for (const Set<uint32_t>::Element *e = info.mutable_components_storage.front(); e; e = e->next()) {
				if (ECS::storage_notify_release_write(e->get())) {
					if (ed.notify_list_release_write.find(e->get()) == -1) {
						ed.notify_list_release_write.push_back(e->get());
					}
				}
			}

			// If set: make sure that the `Child` storage (so the `Hierarchy`
			// storage) is flushed first.
			const int64_t child_index = ed.notify_list_release_write.find(Child::get_component_id());
			if (child_index != -1) {
				SWAP(ed.notify_list_release_write[child_index], ed.notify_list_release_write[0]);
				CRASH_COND(ed.notify_list_release_write[0] != Child::get_component_id());
			}
		}
	}
}

bool Pipeline::is_ready() const {
	return ready;
}

void Pipeline::get_systems_dependencies(SystemExeInfo &p_info) const {
	for (uint32_t i = 0; i < systems_info.size(); i += 1) {
		systems_info[i](p_info);
	}
}

void Pipeline::reset() {
	systems_info.clear();
	systems_exe.clear();
	ready = false;
}

void Pipeline::prepare(World *p_world) {
	// Make sure to reset the `need_changed` for the storages of this world.
	for (uint32_t i = 0; i < p_world->storages.size(); i += 1) {
		if (p_world->storages[i] != nullptr) {
			p_world->storages[i]->reset_changed();
			p_world->storages[i]->set_tracing_change(false);
		}
	}

	// Crete components and databags storages.
	SystemExeInfo info;
	for (uint32_t i = 0; i < systems_info.size(); i += 1) {
		info.clear();
		systems_info[i](info);

		// Create components.
		for (const Set<uint32_t>::Element *e = info.immutable_components.front(); e; e = e->next()) {
			p_world->create_storage(e->get());
		}

		for (const Set<uint32_t>::Element *e = info.mutable_components.front(); e; e = e->next()) {
			p_world->create_storage(e->get());
		}

		for (const Set<uint32_t>::Element *e = info.mutable_components_storage.front(); e; e = e->next()) {
			p_world->create_storage(e->get());
		}

		// Create databags.
		for (const Set<uint32_t>::Element *e = info.immutable_databags.front(); e; e = e->next()) {
			p_world->create_databag(e->get());
		}

		for (const Set<uint32_t>::Element *e = info.mutable_databags.front(); e; e = e->next()) {
			p_world->create_databag(e->get());
		}

		for (const Set<uint32_t>::Element *e = info.need_changed.front(); e; e = e->next()) {
			// Mark as `need_changed` this storage.
			StorageBase *storage = p_world->get_storage(e->get());
			storage->set_tracing_change(true);
		}
	}

	// Set the current `Components` as changed.
	for (uint32_t i = 0; i < p_world->storages.size(); i += 1) {
		if (p_world->storages[i] != nullptr) {
			if (p_world->storages[i]->is_tracing_change()) {
				const EntitiesBuffer entities = p_world->storages[i]->get_stored_entities();
				for (uint32_t e = 0; e < entities.count; e += 1) {
					p_world->storages[i]->notify_changed(entities.entities[e]);
				}
			}
		}
	}
}

void Pipeline::dispatch(World *p_world) {
#ifdef DEBUG_ENABLED
	CRASH_COND_MSG(ready == false, "You can't dispatch a pipeline which is not yet builded. Please call `build`.");
#endif

	p_world->is_dispatching_in_progress = true;

	Hierarchy *hierarchy = static_cast<Hierarchy *>(p_world->get_storage<Child>());
	if (hierarchy) {
		// Flush the hierarchy.
		hierarchy->flush_hierarchy_changes();
	}

	// Process the `TemporarySystem`, if any.
	for (int i = 0; i < int(temporary_systems_exe.size()); i += 1) {
		if (temporary_systems_exe[i](p_world)) {
			temporary_systems_exe.remove(i);
			i -= 1;
		}
	}

	// Dispatch the `System`s.
	for (uint32_t i = 0; i < systems_exe.size(); i += 1) {
		const ExecutionData &ed = systems_exe[i];
		ed.exe(p_world);

		// Notify the `System` released the storage.
		for (uint32_t f = 0; f < ed.notify_list_release_write.size(); f += 1) {
			p_world->get_storage(ed.notify_list_release_write[f])->on_system_release();
		}
	}

	// Clear any generated component storages.
	for (uint32_t c = 0; c < event_generator.size(); c += 1) {
		p_world->get_storage(event_generator[c])->clear();
	}

	// Flush changed.
	if (is_sub_dispatcher == false) {
		for (uint32_t c = 0; c < p_world->storages.size(); c += 1) {
			if (p_world->storages[c] != nullptr) {
				p_world->storages[c]->flush_changed();
			}
		}
	}

	p_world->is_dispatching_in_progress = false;
}
