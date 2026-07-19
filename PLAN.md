# PLAN
I put plan in here. I don't know if it would be implemented or no.

## Removing the differences between global and local scope
### Motivation
Currently global scope is kinda a special scope. It handled differently unlike
other scope. For example it requires me to have OP_GET_GLOBAL. I kinda want 
to unify all of this. But it kinda hard because the thing is in Yue, local variable's
name is not avaiable at runtime because at compile time it's compiled into a slot where
that variable exists which makes local variable access fast.
### TODO
1. Support string interning (this will makes working with string more efficient because we don't need
to allocate string again and again)
2. Variable should use the Yue_Table implementation for variable storing. Every scope will have 
`this` table (it's a keyword) where `this` contains all the variables in current scope. The cool
thing is that we still can have slot accessing (for faster access) either by using hash or the index
in the value (but the table's entries' index must be stable)
