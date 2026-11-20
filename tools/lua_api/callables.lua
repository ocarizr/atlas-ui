---@meta
-- Editor-only declarations; the host injects these tables only into registry modules.

---@class atlas_callable_choice
---@field label string
---@field value integer

---@class atlas_callable_parameter
---@field name string
---@field type 'boolean'|'integer'|'float'|'string'
---@field default? atlas_value
---@field choices? atlas_callable_choice[]

---@class atlas_callable_context
---@field get fun(field: string): atlas_value Read committed model state.
---@field set fun(field: string, value: atlas_value) Actions only: stage a field assignment.
---@field invoke fun(action: string, ...: atlas_value) Actions only: stage a ViewModel action.
---@field emit fun(event: string, ...: atlas_value) Actions only: stage a ViewModel event.
---@field request fun(operation: string, ...: atlas_value) Actions only: stage value-only work.

---@class atlas_callable_descriptor
---@field id string Stable contributor-prefixed ID.
---@field label string Inspector caption.
---@field category string Inspector grouping.
---@field parameters? atlas_callable_parameter[]

---@class atlas_action_descriptor: atlas_callable_descriptor
---@field execute fun(context: atlas_callable_context, ...: atlas_value)

---@class atlas_condition_descriptor: atlas_callable_descriptor
---@field evaluate fun(context: atlas_callable_context, ...: atlas_value): boolean

---@type {register: fun(descriptor: atlas_action_descriptor)}
actions = {}

---@type {register: fun(descriptor: atlas_condition_descriptor)}
navigation_conditions = {}
