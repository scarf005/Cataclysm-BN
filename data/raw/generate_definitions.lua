--[[
    Receives a table and returns sorted array where each value is table of type { k=k, v=v }.

    Sort by key by default, but may also use provided sort function f(a, b)
    where a and b - tables of type {k=k, v=v}
    (the function must return whether a is less than b).
]]--
---@generic A
---@generic B
---@param t table<A, B>
---@param f fun(a: {k: A, v: B}, b: {k: A, v: B}): boolean
---@return {k: A, v: B}[]
local sorted_by = function(t, f)
    if not f then
        f = function(a,b) return (a.k < b.k) end
    end
    local sorted = {}
    for k, v in pairs(t) do
        sorted[#sorted + 1] = {k=k, v=v}
    end
    table.sort( sorted, f )
    return sorted
end

---@param arg_list string[]
local remove_hidden_args = function(arg_list)
    local ret = {}
    for _, arg in pairs(arg_list) do
        if arg == "<this_state>" then
            -- sol::this_state is only visible to C++ side
        else
            ret[#ret + 1] = arg
        end
    end
    return ret
end

local fmt_table = {
    ["bool"] = "boolean",
    ["int"] = "integer",
    ["double"] = "number",
    ["char"] = "string",
}
---@param type string
local fmt_type = function(type)
    return fmt_table[type] or type
end

---@param arg_list string[]
local fmt_arg_list = function(arg_list)
    local ret = ""
    local arg_list = remove_hidden_args(arg_list)
    if #arg_list == 0 then
        return ret
    end
    local is_first = true
    for _, arg in pairs(arg_list) do
        if not is_first then
            ret=ret..","
        end
        ret=ret.." "..arg
        is_first = false
    end
    return ret.." "
end

---@param typename string
---@param ctor string[]
local fmt_one_constructor = function(typename, ctor)
    local fn = "function "..typename..":new("..fmt_arg_list(ctor)..") end"
    if #ctor == 0 then
        return fn
    else
        return "---@param" .. fmt_arg_list(ctor) .. "any \n" .. fn
    end
end

---@generic A
---@generic B
---@param t A[]
---@param f fun(v: A): B
---@return B[]
local map = function(t, f)
    local ret = {}
    for k, v in pairs(t) do
        ret[k] = f(v)
    end
    return ret
end

---@param typename string
---@param ctors table[]
local fmt_constructors = function(typename, ctors)
    if #ctors == 0 then
        return ""
    else
        local ret = "\n\n--[[constructors]]--\n"
        for k,v in pairs(ctors) do
            ret=ret..fmt_one_constructor(typename, v).."\n\n"
        end
        return ret
    end
end


---@param typename string
---@param member table
local fmt_one_member = function(typename, member)
    -- local ret = "#### "..tostring(member.name).."\n";
    local ret = ""
    if member.comment then
        ret=ret.."---"..member.comment.."\n"
    end

    if member.type == "var" then
        local vartype = fmt_type(member.vartype)
        ret=ret.."---@field "..vartype.."\n"
        if member.hasval then
            ret=ret.." value = "..tostring(member.varval).."\n"
        end
        ret=ret.."\n"
    elseif member.type == "func" then
        for _,overload in pairs(member.overloads) do
            local retval = fmt_type(overload.retval)
            if retval ~= "nil" then
                ret=ret.."\n---@return "..retval.."\n"
            end
            ret=ret.."function "..typename..":"..member.name .."("..fmt_arg_list(overload.args)..")"
            ret=ret.." end\n"
        end
    else
        error("Unknown member type "..tostring(member.type))
    end

    return ret
end

---@param typename string?
---@param members table[]
local fmt_members = function(typename, members)
    if #members == 0 then
        return "\n"
    else
        local ret = "\n--[[members]]--\n"

        local members_sorted = sorted_by( members )

        for _,it in pairs(members_sorted) do
            ret=ret..fmt_one_member(typename, it.v).."\n"
        end
        return ret
    end
end

---@param typename string
---@param bases string[]
local fmt_bases = function(typename, bases)
    if #bases == 0 then
        return ""
    else
        return ": "..table.concat(bases, ", ")
    end
end

local fmt_enum_entries = function(typename, entries)
    if next(entries) == nil then
        return "  No entries.\n"
    else
        local ret = ""

        local entries_filtered = {}
        for k,v in pairs(entries) do
            -- TODO: this should not be needed
            if type(v) ~= "table" and type(v) ~= "function" then
                entries_filtered[k] = v;
            end
        end

        local entries_sorted = sorted_by(entries_filtered, function(a,b) return a.v < b.v end)
        for _,it in pairs(entries_sorted) do
            ret=ret.."  "..tostring(it.k).." = "..tostring(it.v)..",\n"
        end
        return ret
    end
end

doc_gen_func.impl = function()
    local ret = "---@meta\n\n"

    local dt = catadoc

    local types_table = dt["#types"]

    local types_sorted = sorted_by(types_table)
    for _,it in pairs(types_sorted) do
        local typename = it.k
        local dt_type = it.v
        local type_comment = dt_type.type_comment
        if type_comment then
            ret = ret .. "---" .. type_comment .. "\n"
        end
        ret = ret.."---@class "..typename


        local bases = dt_type["#bases"]
        local ctors = dt_type["#construct"]
        local members = dt_type["#member"]

        ret = ret
        ..fmt_bases( typename, bases )
            .. "\n" .. typename .. " = {}\n\n"
        ..fmt_constructors( typename, ctors )
        ..fmt_members( typename, members )
    end

    ret = ret.."--[[Enums]]--\n\n"

    local enums_table = dt["#enums"]

    local enums_sorted = sorted_by(enums_table)
    for _,it in pairs(enums_sorted) do
        local typename = it.k
        local dt_type = it.v
        ret = ret.."---@enum "..typename.."\n"

        local entries = dt_type["entries"]

        ret = ret
        .."local ".. typename .." = {\n"
        ..fmt_enum_entries( typename, entries )
        .."}\n\n"
    end

    ret = ret.."-- [[Libraries]]--\n\n"

    local libs_table = dt["#libs"]

    local libs_sorted = sorted_by( libs_table )
    for _,it in pairs(libs_sorted) do
        local typename = it.k
        local dt_lib = it.v
        local lib_comment = dt_lib.lib_comment
        if lib_comment then
            ret = ret.."---"..lib_comment.."\n"
        end
        ret = ret.."---@class "..typename.."\n"

        local members = dt_lib["#member"]

        ret = ret
            .. "\n"..typename .. " = {}\n\n"
            .. fmt_members(typename, members)
    end

    return ret
end
