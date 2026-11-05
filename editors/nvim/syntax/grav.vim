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
syn keyword gravKeyword fn class struct enum type typedef interface abstract namespace
syn keyword gravKeyword constructor extends implements uses new return import export from use test
syn keyword gravKeyword trait destructor operator final
syn keyword gravStorage  let const static readonly public private protected weak
syn keyword gravKeyword async await

" Control flow
syn keyword gravConditional if else switch case default match
syn keyword gravRepeat     while for do in step
syn keyword gravStatement  break continue defer
syn keyword gravException  try catch finally throw
syn keyword gravOperatorKw as is sizeof

" Built-in types: the core set plus the widened numeric / binary types. These are
" only types in type position (resolved by the parser), so they are matched, not
" reserved — but highlighting them as types reads well.
syn keyword gravType int float bool string void binary
syn keyword gravType long int64 int32 int16 int8 signed
syn keyword gravType uint unsigned uint64 ulong uint32 uint16 uint8 byte
syn keyword gravType double float64 float32 any auto

" Constants
syn keyword gravBoolean true false
syn keyword gravConstant null self this Self super

" Built-in functions
syn keyword gravBuiltin print println typename isInstance str input argc argv len
syn keyword gravBuiltin free panic assert assertEq exit cwd env

" Numbers: 42, 1_000_000, 0xff_ff, 0b1010, 2.5, 1e9, 2.5e-3
syn match gravNumber "\<\d\%(_\=\d\)*\>"
syn match gravNumber "\<0[xX]\x\%(_\=\x\)*\>"
syn match gravNumber "\<0[bB][01]\%(_\=[01]\)*\>"
syn match gravFloat  "\<\d\%(_\=\d\)*\.\d\+\%([eE][-+]\=\d\+\)\=\>"
syn match gravFloat  "\<\d\%(_\=\d\)*[eE][-+]\=\d\+\>"

" Strings with ${...} interpolation. The interpolated expression highlights its
" own numbers / booleans / builtins / types / operators (not nested strings).
syn cluster gravInterpInner contains=gravNumber,gravFloat,gravBoolean,gravConstant,gravBuiltin,gravType,gravOperator
syn match  gravFormatSpec ":\%([<>^]\=+\=0\=\d*\%(\.\d\+\)\=[dfexXobs%]\=\)\ze}" contained   " ${x:.2f}
syn region gravInterp matchgroup=gravInterpDelim start="\${" end="}" contained contains=@gravInterpInner,gravFormatSpec
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

" Semantic matches. `syn keyword` always wins at its own position, so names
" after a keyword are matched with a look-behind (the match starts at the name).
" Among matches starting at the same column, the one defined last wins.
syn match gravFuncCall    "\<\w\+\ze\s*\%(::<[^>]*>\)\=\s*("     " foo( / foo::<int>(
syn match gravField       "\%(\.\|->\)\@<=\w\+"                  " obj.field
syn match gravMethodCall  "\%(\.\)\@<=\w\+\ze\s*\%(::<[^>]*>\)\=\s*("   " obj.method(
syn match gravEnumMember  "\%(\<\u\w*\.\)\@<=\u\w*\>"            " Enum.Member
syn match gravTypeRef     "\%(\<\%(new\|extends\|implements\|as\|is\)\s\+\)\@<=\%(\w\+\.\)*\u\w*"
syn match gravTypeRef     "\%(\%(^\|[^:]\):\s*\)\@<=\%(\w\+\.\)*\u\w*"   " x: Type
syn match gravTypeRef     "\%(->\s*\)\@<=\%(\w\+\.\)*\u\w*"              " -> Type
syn match gravTypeRef     "\%([<,]\s*\)\@<=\u\w*\ze\s*[<>,\[*]"           " Array<Type>, Map<K, V>
syn match gravTypeParam   "\%(\<\%(class\|struct\|enum\|interface\|trait\|type\|fn\)\s\+\w\+\s*<\s*\%(\w\+\s*\%(:\s*\w\+\s*\)\=,\s*\)*\)\@<=\u\w*"
syn match gravFuncDef     "\%(\<fn\s\+\)\@<=\w\+"                " fn name
syn match gravFuncDef     "\%(\<fn\s\+operator\s*\)\@<=\S\{1,3}\ze\s*("    " fn operator==
syn match gravTypeDef     "\%(\<\%(class\|struct\|enum\|interface\|trait\|type\)\s\+\)\@<=\w\+"
syn match gravNamespace   "\%(\<\%(namespace\|use\)\s\+\)\@<=\w\+\%(\%(\.\|::\)\w\+\)*"
syn match gravNamespace   "\%(\<import\s\+"[^"]*"\s\+as\s\+\)\@<=\w\+"
syn match gravEnumMember  "\%(\<case\s\+\%(\w\+\.\)*\)\@<=\u\w*"   " case Circle(r):
syn match gravImportPath  +\%(\<\%(import\|from\)\s*\)\@<="[^"]*"+
syn match gravTestName    +\%(^\s*test\s\+\)\@<="[^"]*"+
syn match gravLabel       "^\s*\zs\w\+\ze\s*:\s*\%(for\|while\|do\)\>"      " outer: for (...)
syn match gravLabel       "\%(\<\%(break\|continue\)\s\+\)\@<=\w\+"        " break outer
syn match gravNamedArg    "\%([(,]\s*\)\@<=\w\+\ze\s*:[^:]"                " f(name: value)
syn match gravSpread      "\.\.\."                                         " f(...xs)

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
hi def link gravFormatSpec    SpecialChar
hi def link gravLabel         Label
hi def link gravNamedArg      Identifier
hi def link gravSpread        Operator
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
hi def link gravImportPath    Include
hi def link gravNamespace     Identifier
hi def link gravTypeParam     Type
hi def link gravTestName      Title

let b:current_syntax = "grav"
