-- The native owner validates instance generation and registration order before entering here.
-- This function only routes a validated event; behavior remains in the target instance.
---@param target table<string, function>
---@param handler_name string
---@param ... any
---@return ... any
return function(target, handler_name, ...)
    local handler = target[handler_name]
    if handler ~= nil then
        return handler(...)
    end
end
