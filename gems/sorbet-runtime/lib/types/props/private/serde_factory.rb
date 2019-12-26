# frozen_string_literal: true
# typed: true

module T::Props
  module Private
    module SerdeFactory
      extend T::Sig

      NILABLE_DUP = ->(x) {x&.dup}.freeze
      NONNIL_DUP = ->(x) {x.dup}.freeze
      SERIALIZE_PROPS = ->(x) {x&.serialize}.freeze
      DYNAMIC_DEEP_CLONE = T::Props::Utils.method(:deep_clone_object).to_proc.freeze
      NO_TRANSFORM_TYPES = [TrueClass, FalseClass, NilClass, Symbol, String, Numeric].freeze

      class Mode < T::Enum
        enums do
          SERIALIZE = new
          DESERIALIZE = new
        end
      end

      sig do
        params(
          type: T.any(T::Types::Base, Module),
          mode: Mode,
          nilable: T::Boolean,
        )
        .returns(T.nilable(T.proc.params(val: T.untyped).returns(T.untyped)))
        .checked(:never)
      end
      def self.generate(type, mode, nilable)
        case type
        when T::Types::TypedArray
          inner = generate(type.type, mode, false)
          if inner.nil?
            nilable ? NILABLE_DUP : NONNIL_DUP
          else
            nilable ? ->(x) {x&.map(&inner)} : ->(x) {x.map(&inner)}
          end
        when T::Types::TypedSet
          inner = generate(type.type, mode, false)
          if inner.nil?
            nilable ? NILABLE_DUP : NONNIL_DUP
          else
            nilable ? ->(x) {x && Set.new(x, &inner)} : ->(x) {Set.new(x, &inner)}
          end
        when T::Types::TypedHash
          keys = generate(type.keys, mode, false)
          values = generate(type.values, mode, false)
          if keys.nil? && values.nil?
            nilable ? NILABLE_DUP : NONNIL_DUP
          elsif keys.nil?
            nilable ? ->(x) {x&.transform_values(&values)} : ->(x) {x.transform_values(&values)}
          elsif values.nil?
            nilable ? ->(x) {x&.transform_keys(&keys)} : ->(x) {x.transform_keys(&keys)}
          elsif nilable
            ->(x) {x&.each_with_object({}) {|(k,v),h| h[keys.call(k)] = values.call(v)}}
          else
            ->(x) {x.each_with_object({}) {|(k,v),h| h[keys.call(k)] = values.call(v)}}
          end
        when T::Types::Simple
          raw = type.raw_type
          if NO_TRANSFORM_TYPES.any? {|cls| raw <= cls}
            nil
          elsif raw < T::Props::Serializable
            handle_serializable_subtype(raw, mode, nilable)
          elsif raw.singleton_class < T::Props::CustomType
            handle_custom_type(raw, mode, nilable)
          else
            DYNAMIC_DEEP_CLONE
          end
        when T::Types::Union
          non_nil_type = T::Utils.unwrap_nilable(type)
          if non_nil_type
            generate(non_nil_type, mode, true)
          else
            DYNAMIC_DEEP_CLONE
          end
        else
          if type.singleton_class < T::Props::CustomType
            # Sometimes this comes wrapped in a T::Types::Simple and sometimes not
            handle_custom_type(type, mode, nilable)
          else
            DYNAMIC_DEEP_CLONE
          end
        end
      end

      sig do
        params(
          type: T.untyped,
          mode: Mode,
          nilable: T::Boolean,
        )
        .returns(T.proc.params(val: T.untyped).returns(T.untyped))
        .checked(:never)
      end
      private_class_method def self.handle_custom_type(type, mode, nilable)
        case mode
        when Mode::SERIALIZE
          if nilable
            ->(x) {x.nil? ? nil : T::Props::CustomType.checked_serialize(type, x)}
          else
            ->(x) {T::Props::CustomType.checked_serialize(type, x)}
          end
        when Mode::DESERIALIZE
          if nilable
            ->(x) {x.nil? ? nil : type.deserialize(x)}
          else
            type.method(:deserialize).to_proc
          end
        else
          T.absurd(mode)
        end
      end

      sig do
        params(
          type: T.untyped,
          mode: Mode,
          nilable: T::Boolean,
        )
        .returns(T.proc.params(val: T.untyped).returns(T.untyped))
        .checked(:never)
      end
      private_class_method def self.handle_serializable_subtype(type, mode, nilable)
        case mode
        when Mode::SERIALIZE
          SERIALIZE_PROPS
        when Mode::DESERIALIZE
          if type.method(:from_hash).owner == T::Props::Serializable::ClassMethods
            # Skip some indirection unless we have to support an override;
            # benchmarks support a real difference here given a collection of
            # subdocs
            if nilable
              lambda do |h|
                if h.nil?
                  nil
                else
                  t = type.allocate
                  t.deserialize(h)
                  t
                end
              end
            else
              lambda do |h|
                t = type.allocate
                t.deserialize(h)
                t
              end
            end
          else
            if nilable
              ->(x) {x.nil? ? nil : type.from_hash(x)}
            else
              type.method(:from_hash).to_proc
            end
          end
        else
          T.absurd(mode)
        end
      end
    end
  end
end

