# frozen_string_literal: true
# typed: true

module T::Types
  # Validates that an object belongs to the specified class.
  class Extends < Base
    attr_reader :type

    def initialize(type)
      @type = type
    end

    # @override Base
    def name
      "T.extends(#{@type})"
    end

    # @override Base
    def valid?(obj)
      obj.is_a?(Module) && obj.singleton_class.included_modules.include? @type
    end

    # @override Base
    def subtype_of_single?(other)
      case other
      when Extends
        @type <= other.type
      else
        false
      end
    end

    # @override Base
    def describe_obj(obj)
      obj.inspect
    end
  end
end
