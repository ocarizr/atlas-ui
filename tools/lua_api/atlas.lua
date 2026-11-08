---@meta
---@diagnostic disable: missing-fields
-- Editor-only declarations for the current embedded API; never execute or package this file.

---@alias atlas_value boolean|integer|number|string
---@alias atlas_field integer Runtime-validated field slot; cache only within the current schema.
---@alias atlas_action integer Runtime-validated action slot; distinct from fields and events.
---@alias atlas_event integer Runtime-validated event slot; distinct from fields and actions.

---@class atlas_model
---@field field fun(name: string): atlas_field Resolve a field during script initialization.
---@field action fun(name: string): atlas_action Resolve an action during script initialization.
---@field event fun(name: string): atlas_event Resolve an event during script initialization.
---@field get fun(slot: atlas_field): atlas_value Read the committed model snapshot.
---@field set fun(slot: atlas_field, value: atlas_value) Stage an exact-type assignment.
---@field add_integer fun(slot: atlas_field, delta: integer) Stage a checked integer increment.
---@field add_number fun(slot: atlas_field, delta: number) Stage a finite numeric increment.
---@field invoke fun(slot: atlas_action, ...: atlas_value) Stage an action with its schema signature.
---@field emit fun(slot: atlas_event, ...: atlas_value) Stage an event with its schema signature.
---@field request fun(name: string, ...: atlas_value): integer Request host work; never run it in Lua.
---@field subscribe fun(slot: atlas_field|atlas_action|atlas_event, handler: string): integer Stage a scoped listener until synchronization.
---@field unsubscribe fun(token: integer) Stage retirement of a listener from this instance.
---@type atlas_model
model = {}

---@class atlas_native
---@field call fun(name: string, ...: atlas_value): ...: atlas_value Call a typed inexpensive project service in a domain.
---@type atlas_native
native = {}

-- Only domain instances have this catalog-only loader. Standalone sessions omit require/native.
---@type fun(identity: string): any
require = function(_) end

-- Hosts choose handler names/signatures through session.call; no director API is implied.
-- Registered observers receive: field(value), action/event(...schema parameters).
-- Optional on_result(id, success, ...values) receives submission-order native work results.
