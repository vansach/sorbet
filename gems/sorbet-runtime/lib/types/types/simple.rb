# frozen_string_literal: true
# typed: true

module T::Types
  # Validates that an object belongs to the specified class.
  class Simple < Base
    attr_reader :raw_type

    def initialize(raw_type)
      @raw_type = raw_type
    end

    # @override Base
    def name
      @raw_type.name
    end

    # @override Base
    def valid?(obj)
      obj.is_a?(@raw_type)
    end

    # @override Base
    private def subtype_of_single?(other)
      case other
      when Simple
        @raw_type <= other.raw_type
      else
        false
      end
    end

    module Private
      class Nil < Simple

        def initialize(raw_type)
          raise ArgumentError.new("#{raw_type} is not NilClass") unless raw_type == NilClass
          super
        end

        # @override Simple
        def valid?(obj)
          # This is ~2x as fast as obj.is_a?(NilClass) on Ruby 2.6
          nil == obj
        end

        INSTANCE = new(NilClass).freeze
      end
    end
  end
end
