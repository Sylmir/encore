{-# LANGUAGE NamedFieldPuns #-}

{-| The context that several of the Translatable typeclasses use
for compiling. It is used to generate new symbols for temporary
variables, store the mappings from encore variables to c variables
and to keep track of which class we're translating at the
moment. -}

module CodeGen.Context (
  Context,
  empty,
  subst_add,
  subst_lkp,
  gen_sym) where

import AST.AST
import Identifiers
import Types
import Data.Maybe
import Control.Monad.State

import qualified CCode.Main as C

type NextSym = Int

type VarSubTable = [(Name, C.CCode C.Lval)] -- variable substitutions (for supporting, for instance, nested var decls)

data Context = Context VarSubTable NextSym

empty :: Context
empty = Context [] 0

gen_sym :: State Context String
gen_sym = do
  c <- get
  case c of
    Context s n ->
        do
          put $ Context s (n+1)
          return $ "_tmp" ++ show n
  
subst_add :: Context -> Name -> C.CCode C.Lval -> Context
subst_add (Context s nxt) na lv = Context ((na,lv):s) nxt

subst_lkp :: Context -> Name -> Maybe (C.CCode C.Lval)
subst_lkp (Context s _) n = lookup n s