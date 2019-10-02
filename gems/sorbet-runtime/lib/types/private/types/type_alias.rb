# frozen_string_literal: true
# typed: true

module T::Private::Types
  # Wraps a proc for a type alias to defer its evaluation.
  class TypeAlias < T::Types::Base

    def initialize(callable)
      @callable = callable
    end

    # @override Base
    def name
      aliased_type.name
    end

    # @override Base
    def valid?(obj)
      aliased_type.valid?(obj)
    end

    # @override Base
    private def subtype_of_single?(other)
      # TODO
      aliased_type.send(:subtype_of_single?, other)
    end

    private def aliased_type
      @aliased_type ||= T::Utils.coerce(@callable.call)
    end
  end
end
