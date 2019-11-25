# typed: true

class Module
  include T::Sig
end

module List
  extend T::Helpers
  extend T::Generic
  sealed!

  Elem = type_member(:out)
end

class Cons < T::Struct
  extend T::Generic
  include List

  Elem = type_member

  prop :head, Elem
  prop :tail, List[Elem]
end

class Nil
  extend T::Generic
  include List

  Elem = type_member(fixed: T.noreturn)
end

# class NilType < T::Enum
#   extend T::Generic
#   include List
#
#   # TODO(jez) Probably want to allow this
#   Elem = type_member(fixed: T.noreturn) # error: instances of the enum
#
#   enums do
#     Nil = new
#   end
# end

# sig {params(xs: T.all(Cons[T.untyped], List[Integer])).void}
sig {params(xs: List[Integer]).void}
# sig {params(xs: T.any(Cons[Integer], NilType)).void}
def foo(xs)
  T.reveal_type(xs)

  case xs
  when Cons
    T.reveal_type(xs)
  when Nil
    T.reveal_type(xs)
  else
    T.absurd(xs)
  end
end
