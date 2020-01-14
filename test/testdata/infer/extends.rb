# typed: true

module MyMod
  def self.foo; end
end

class A
  extend MyMod
end
class B
end

extend T::Sig
sig {params(x: T.extends(MyMod)).void}
def bar(x)
  x.foo
  x.quux  # error: Method `quux` does not exist
end

bar(A)
bar(B)  # error: Expected `T.extends(MyMod)` but found `B`
