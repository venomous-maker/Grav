" Vim syntax file for the Grav language (.grav)
" Maintainer: generated for the Grav compiler (gravc)

if exists("b:current_syntax")
  finish
endif

" Comments
syn keyword gravTodo contained TODO FIXME XXX NOTE
syn match   gravLineComment "//.*$" contains=gravTodo
syn region  gravBlockComment start="/\*" end="\*/" contains=gravTodo

" Declaration / structure keywords
syn keyword gravKeyword fn class struct enum type interface abstract namespace
syn keyword gravKeyword constructor extends implements new return import export
syn keyword gravStorage  let const static readonly public private protected
syn keyword gravKeyword async await

" Control flow
syn keyword gravConditional if else switch case default match
syn keyword gravRepeat     while for do in
syn keyword gravStatement  break continue
syn keyword gravException  try catch finally throw
syn keyword gravOperatorKw as is sizeof

" Built-in types
syn keyword gravType int float bool string void

" Constants
syn keyword gravBoolean true false
syn keyword gravConstant null self this

" Built-in functions
syn keyword gravBuiltin print typename isInstance str input argc argv len

" Numbers
syn match gravNumber "\<\d\+\>"
syn match gravFloat  "\<\d\+\.\d\+\>"

" Strings with ${...} interpolation. The interpolated expression highlights its
" own numbers / booleans / builtins / types / operators (not nested strings).
syn cluster gravInterpInner contains=gravNumber,gravFloat,gravBoolean,gravConstant,gravBuiltin,gravType,gravOperator
syn region gravInterp matchgroup=gravInterpDelim start="\${" end="}" contained contains=@gravInterpInner
syn region gravString start=+"+ skip=+\\"+ end=+"+ contains=gravInterp,gravEscape
syn match  gravEscape "\\[ntr\\\"$]" contained

" Inline C escape hatch: %{ ... %} — highlight the body with the real C syntax.
syn include @gravCSyntax syntax/c.vim
if exists("b:current_syntax")
  unlet b:current_syntax
endif
syn region gravCBlock matchgroup=gravCBlockDelim start="%{" end="%}" keepend contains=@gravCSyntax

" Decorators
syn match gravDecorator "@\w\+"

" Semantic matches (later definitions win on overlap; `syn keyword` always wins,
" so control-flow keywords and built-ins keep their own colors).
syn match gravFuncCall   "\<\w\+\ze\s*("                       " foo(
syn match gravTypeRef     "\<\%(new\|extends\|implements\)\s\+\zs\%(\w\|\.\)\+" " new Type / extends Base
syn match gravField       "\%(\.\|->\)\zs\w\+"                  " obj.field / ptr->field
syn match gravMethodCall  "\%(\.\|->\)\zs\w\+\ze\s*("           " obj.method(
syn match gravEnumMember  "\<\u\w*\.\zs\u\w*"                   " Enum.Member
syn match gravFuncDef     "\<fn\s\+\zs\w\+"                     " fn name
syn match gravTypeDef     "\<\%(class\|struct\|enum\|interface\|type\|namespace\)\s\+\zs\w\+"

" Operators
syn match gravOperator "[-+*/%<>=!&|^~?:.]"

hi def link gravLineComment   Comment
hi def link gravBlockComment  Comment
hi def link gravTodo          Todo
hi def link gravKeyword       Keyword
hi def link gravStorage       StorageClass
hi def link gravConditional   Conditional
hi def link gravRepeat        Repeat
hi def link gravStatement     Statement
hi def link gravException     Exception
hi def link gravOperatorKw    Operator
hi def link gravType          Type
hi def link gravBoolean       Boolean
hi def link gravConstant      Constant
hi def link gravBuiltin       Function
hi def link gravNumber        Number
hi def link gravFloat         Float
hi def link gravString        String
hi def link gravEscape        SpecialChar
hi def link gravInterp        Special
hi def link gravInterpDelim   SpecialChar
hi def link gravCBlock        PreProc
hi def link gravCBlockDelim   Special
hi def link gravDecorator     PreProc
hi def link gravOperator      Operator
hi def link gravFuncDef       Function
hi def link gravFuncCall      Function
hi def link gravMethodCall    Function
hi def link gravTypeDef       Structure
hi def link gravTypeRef       Type
hi def link gravField         Identifier
hi def link gravEnumMember    Constant

let b:current_syntax = "grav"
