-- Shared rule tested directly and imported by the catalog-loaded behavior.
return function(value, limit)
    return math.max(0, math.min(value, limit))
end
