{-| The context that several of the Translatable typeclasses use
for compiling. It is used to generate new symbols for temporary
variables, store the mappings from encore variables to c variables
and to keep track of which class we're translating at the
moment. -}

module CodeGen.Context (
  Context,
  ExecContext(..),
  new,
  newWithForwarding,
  substAdd,
  substLkp,
  substRem,
  genNamedSym,
  genSym,
  getGlobalFunctionNames,
  lookupFunction,
  lookupField,
  lookupMethod,
  lookupCalledType,
  setMtdCtx,
  setFunCtx,
  setClsCtx,
  getExecCtx,
  isAsyncForward,
  isFlowCtx,
  setIsFlowCtx,
  newWithFlow
) where

import Identifiers
import Types
import AST.AST
import Control.Monad.State
import qualified CodeGen.ClassTable as Tbl

import qualified CCode.Main as C
import CodeGen.CCodeNames

type NextSym = Int

type VarSubTable = [(Name, C.CCode C.Lval)] -- variable substitutions (for supporting, for instance, nested var decls)

data ExecContext =
    FunctionContext{fun :: Function}
  | MethodContext  {mdecl :: MethodDecl}
  | ClosureContext {cls :: Expr} -- for checking closure in the future.
  | Empty
    deriving(Show)

data Context = Context {
  varSubTable  :: VarSubTable,
  nextSym      :: NextSym,
  execContext  :: ExecContext,
  programTbl   :: Tbl.ProgramTable,
  withForward  :: Bool,
  {- 
    Indicate if the function we are translating is in a flow context or not.
    A flow context is necessary when we have to translate flow calls of a 
    parametric function. Consider :
  
    active class Foo
      def f[sharable t](x : t) : t
        x
      end
    end
    
    active class Main
      def main() : unit
        var x = new Foo()
        var y = x!!f(2)
        var z = x!!f(y)
        println(get*(y))
        println(get*(z))
      end
    end

    The asynchronous flow call to f(2) would result in a Flow[int] with a result
    type of VALUE. This isn't because we are calling f with an int and not a 
    Flow, but because the result type of f isn't a Flow ; instead it is "t", a 
    type variable. This isn't problematic here, but it becomes problematic with 
    in second call.

    The asynchronous call to f(y) would result in a Flow[int] with a result type
    of VALUE, because the result type of f isn't a Flow. Even if "y" evaluates 
    to a Flow, f returns something of type "t" which, as far as f is concerned,
    isn't a Flow.

    To resolve this problem, we generate a specialized version of generic 
    functions, when necessary. When a generic function is called in an 
    asynchronous way, we resolve the type parameters ; if the return of the 
    generic function evaluates to Flow[t], we call the specialized version. 
    
    This field is updated every time we translate a function / method. It 
    indicates is we are generating the specialized version of the function or
    the normal version. This is used in the translation of MessageSendFlow, so
    we know which version to call.
  -}
  isFlow       :: Bool
}

programTable :: Context -> Tbl.ProgramTable
programTable Context{programTbl} = programTbl

new :: VarSubTable -> Tbl.ProgramTable -> Context
new subs table = Context {
    varSubTable = subs
    ,nextSym = 0
    ,execContext = Empty
    ,programTbl = table
    ,withForward = False
    ,isFlow = False
  }

newWithForwarding subs table = Context {
    varSubTable = subs
    ,nextSym = 0
    ,execContext = Empty
    ,programTbl = table
    ,withForward = True
    ,isFlow = False
  }

newWithFlow subs table = Context {
  varSubTable = subs
    ,nextSym = 0
    ,execContext = Empty
    ,programTbl = table
    ,withForward = False
    ,isFlow = True
  }

isAsyncForward :: Context -> Bool
isAsyncForward Context{withForward} = withForward

genNamedSym :: String -> State Context String
genNamedSym name = do
  let (_, name') = fixPrimes name
  c <- get
  case c of
    ctx@Context{nextSym} ->
        do put $ ctx{nextSym = nextSym + 1}
           return $ "_" ++ name' ++ "_" ++ show nextSym

genSym :: State Context String
genSym = genNamedSym "tmp"

substAdd :: Context -> Name -> C.CCode C.Lval -> Context
substAdd ctx@Context{varSubTable} na lv = ctx{varSubTable = ((na,lv):varSubTable)}

substRem :: Context -> Name -> Context
substRem ctx@Context{varSubTable = []} na = ctx
substRem ctx@Context{varSubTable = ((na, lv):s)} na'
     | na == na'  = ctx{varSubTable = s}
     | na /= na'  = substAdd (substRem ctx{varSubTable = s} na') na lv

substLkp :: Context -> QualifiedName -> Maybe (C.CCode C.Lval)
substLkp ctx@Context{varSubTable} QName{qnspace = Nothing, qnlocal} = lookup qnlocal varSubTable
substLkp ctx@Context{varSubTable} QName{qnspace = Just ns, qnlocal}
     | isEmptyNamespace ns = lookup qnlocal varSubTable
     | otherwise = Nothing

setExecCtx :: Context -> ExecContext -> Context
setExecCtx ctx execContext = ctx{execContext}

setFunCtx :: Context -> Function -> Context
setFunCtx ctx fun = ctx{execContext = FunctionContext{fun}}

setMtdCtx :: Context -> MethodDecl -> Context
setMtdCtx ctx mdecl = ctx{execContext = MethodContext{mdecl}}

setClsCtx :: Context -> Expr -> Context
setClsCtx ctx cls = ctx{execContext = ClosureContext{cls}}

getExecCtx :: Context -> ExecContext
getExecCtx ctx@Context{execContext} = execContext

lookupField :: Type -> Name -> Context -> FieldDecl
lookupField ty f = Tbl.lookupField ty f . programTable

lookupMethod :: Type -> Name -> Context -> FunctionHeader
lookupMethod ty m = Tbl.lookupMethod ty m . programTable

lookupCalledType :: Type -> Name -> Context -> Type
lookupCalledType ty m = Tbl.lookupCalledType ty m . programTable

lookupFunction :: QualifiedName -> Context -> (C.CCode C.Name, FunctionHeader)
lookupFunction fname = Tbl.lookupFunction fname . programTable

getGlobalFunctionNames :: Context -> [QualifiedName]
getGlobalFunctionNames = Tbl.getGlobalFunctionNames . programTable

isFlowCtx :: Context -> Bool
isFlowCtx ctx@Context{isFlow} = isFlow

setIsFlowCtx :: Context -> Bool -> Context
setIsFlowCtx ctx isFlow = ctx{isFlow}