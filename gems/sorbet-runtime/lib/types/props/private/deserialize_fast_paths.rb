# frozen_string_literal: true
# typed: false

module T::Props
  module Private
    module DeserializeFastPaths

      # 91% of Chalk::ODM::Document subclasses in pay-server have <= 20 props as of writing.
      MAX_PROPS = 20

      # Generated with:
      #
      # (0..19).each { |i| puts "#{i} => lambda do |hash|\n  found = #{i+1}"; (0..i).each {|j| puts "  found -= 1 unless __prop_deserializer_#{j}(hash)"}; puts "  found\nend,\n\n"; }
      INNER_DESERIALIZE_BY_PROP_COUNT = {
        0 => lambda do |hash|
          found = 1
          found -= 1 unless __prop_deserializer_0(hash)
          found
        end,

        1 => lambda do |hash|
          found = 2
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found
        end,

        2 => lambda do |hash|
          found = 3
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found
        end,

        3 => lambda do |hash|
          found = 4
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found -= 1 unless __prop_deserializer_3(hash)
          found
        end,

        4 => lambda do |hash|
          found = 5
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found -= 1 unless __prop_deserializer_3(hash)
          found -= 1 unless __prop_deserializer_4(hash)
          found
        end,

        5 => lambda do |hash|
          found = 6
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found -= 1 unless __prop_deserializer_3(hash)
          found -= 1 unless __prop_deserializer_4(hash)
          found -= 1 unless __prop_deserializer_5(hash)
          found
        end,

        6 => lambda do |hash|
          found = 7
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found -= 1 unless __prop_deserializer_3(hash)
          found -= 1 unless __prop_deserializer_4(hash)
          found -= 1 unless __prop_deserializer_5(hash)
          found -= 1 unless __prop_deserializer_6(hash)
          found
        end,

        7 => lambda do |hash|
          found = 8
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found -= 1 unless __prop_deserializer_3(hash)
          found -= 1 unless __prop_deserializer_4(hash)
          found -= 1 unless __prop_deserializer_5(hash)
          found -= 1 unless __prop_deserializer_6(hash)
          found -= 1 unless __prop_deserializer_7(hash)
          found
        end,

        8 => lambda do |hash|
          found = 9
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found -= 1 unless __prop_deserializer_3(hash)
          found -= 1 unless __prop_deserializer_4(hash)
          found -= 1 unless __prop_deserializer_5(hash)
          found -= 1 unless __prop_deserializer_6(hash)
          found -= 1 unless __prop_deserializer_7(hash)
          found -= 1 unless __prop_deserializer_8(hash)
          found
        end,

        9 => lambda do |hash|
          found = 10
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found -= 1 unless __prop_deserializer_3(hash)
          found -= 1 unless __prop_deserializer_4(hash)
          found -= 1 unless __prop_deserializer_5(hash)
          found -= 1 unless __prop_deserializer_6(hash)
          found -= 1 unless __prop_deserializer_7(hash)
          found -= 1 unless __prop_deserializer_8(hash)
          found -= 1 unless __prop_deserializer_9(hash)
          found
        end,

        10 => lambda do |hash|
          found = 11
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found -= 1 unless __prop_deserializer_3(hash)
          found -= 1 unless __prop_deserializer_4(hash)
          found -= 1 unless __prop_deserializer_5(hash)
          found -= 1 unless __prop_deserializer_6(hash)
          found -= 1 unless __prop_deserializer_7(hash)
          found -= 1 unless __prop_deserializer_8(hash)
          found -= 1 unless __prop_deserializer_9(hash)
          found -= 1 unless __prop_deserializer_10(hash)
          found
        end,

        11 => lambda do |hash|
          found = 12
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found -= 1 unless __prop_deserializer_3(hash)
          found -= 1 unless __prop_deserializer_4(hash)
          found -= 1 unless __prop_deserializer_5(hash)
          found -= 1 unless __prop_deserializer_6(hash)
          found -= 1 unless __prop_deserializer_7(hash)
          found -= 1 unless __prop_deserializer_8(hash)
          found -= 1 unless __prop_deserializer_9(hash)
          found -= 1 unless __prop_deserializer_10(hash)
          found -= 1 unless __prop_deserializer_11(hash)
          found
        end,

        12 => lambda do |hash|
          found = 13
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found -= 1 unless __prop_deserializer_3(hash)
          found -= 1 unless __prop_deserializer_4(hash)
          found -= 1 unless __prop_deserializer_5(hash)
          found -= 1 unless __prop_deserializer_6(hash)
          found -= 1 unless __prop_deserializer_7(hash)
          found -= 1 unless __prop_deserializer_8(hash)
          found -= 1 unless __prop_deserializer_9(hash)
          found -= 1 unless __prop_deserializer_10(hash)
          found -= 1 unless __prop_deserializer_11(hash)
          found -= 1 unless __prop_deserializer_12(hash)
          found
        end,

        13 => lambda do |hash|
          found = 14
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found -= 1 unless __prop_deserializer_3(hash)
          found -= 1 unless __prop_deserializer_4(hash)
          found -= 1 unless __prop_deserializer_5(hash)
          found -= 1 unless __prop_deserializer_6(hash)
          found -= 1 unless __prop_deserializer_7(hash)
          found -= 1 unless __prop_deserializer_8(hash)
          found -= 1 unless __prop_deserializer_9(hash)
          found -= 1 unless __prop_deserializer_10(hash)
          found -= 1 unless __prop_deserializer_11(hash)
          found -= 1 unless __prop_deserializer_12(hash)
          found -= 1 unless __prop_deserializer_13(hash)
          found
        end,

        14 => lambda do |hash|
          found = 15
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found -= 1 unless __prop_deserializer_3(hash)
          found -= 1 unless __prop_deserializer_4(hash)
          found -= 1 unless __prop_deserializer_5(hash)
          found -= 1 unless __prop_deserializer_6(hash)
          found -= 1 unless __prop_deserializer_7(hash)
          found -= 1 unless __prop_deserializer_8(hash)
          found -= 1 unless __prop_deserializer_9(hash)
          found -= 1 unless __prop_deserializer_10(hash)
          found -= 1 unless __prop_deserializer_11(hash)
          found -= 1 unless __prop_deserializer_12(hash)
          found -= 1 unless __prop_deserializer_13(hash)
          found -= 1 unless __prop_deserializer_14(hash)
          found
        end,

        15 => lambda do |hash|
          found = 16
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found -= 1 unless __prop_deserializer_3(hash)
          found -= 1 unless __prop_deserializer_4(hash)
          found -= 1 unless __prop_deserializer_5(hash)
          found -= 1 unless __prop_deserializer_6(hash)
          found -= 1 unless __prop_deserializer_7(hash)
          found -= 1 unless __prop_deserializer_8(hash)
          found -= 1 unless __prop_deserializer_9(hash)
          found -= 1 unless __prop_deserializer_10(hash)
          found -= 1 unless __prop_deserializer_11(hash)
          found -= 1 unless __prop_deserializer_12(hash)
          found -= 1 unless __prop_deserializer_13(hash)
          found -= 1 unless __prop_deserializer_14(hash)
          found -= 1 unless __prop_deserializer_15(hash)
          found
        end,

        16 => lambda do |hash|
          found = 17
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found -= 1 unless __prop_deserializer_3(hash)
          found -= 1 unless __prop_deserializer_4(hash)
          found -= 1 unless __prop_deserializer_5(hash)
          found -= 1 unless __prop_deserializer_6(hash)
          found -= 1 unless __prop_deserializer_7(hash)
          found -= 1 unless __prop_deserializer_8(hash)
          found -= 1 unless __prop_deserializer_9(hash)
          found -= 1 unless __prop_deserializer_10(hash)
          found -= 1 unless __prop_deserializer_11(hash)
          found -= 1 unless __prop_deserializer_12(hash)
          found -= 1 unless __prop_deserializer_13(hash)
          found -= 1 unless __prop_deserializer_14(hash)
          found -= 1 unless __prop_deserializer_15(hash)
          found -= 1 unless __prop_deserializer_16(hash)
          found
        end,

        17 => lambda do |hash|
          found = 18
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found -= 1 unless __prop_deserializer_3(hash)
          found -= 1 unless __prop_deserializer_4(hash)
          found -= 1 unless __prop_deserializer_5(hash)
          found -= 1 unless __prop_deserializer_6(hash)
          found -= 1 unless __prop_deserializer_7(hash)
          found -= 1 unless __prop_deserializer_8(hash)
          found -= 1 unless __prop_deserializer_9(hash)
          found -= 1 unless __prop_deserializer_10(hash)
          found -= 1 unless __prop_deserializer_11(hash)
          found -= 1 unless __prop_deserializer_12(hash)
          found -= 1 unless __prop_deserializer_13(hash)
          found -= 1 unless __prop_deserializer_14(hash)
          found -= 1 unless __prop_deserializer_15(hash)
          found -= 1 unless __prop_deserializer_16(hash)
          found -= 1 unless __prop_deserializer_17(hash)
          found
        end,

        18 => lambda do |hash|
          found = 19
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found -= 1 unless __prop_deserializer_3(hash)
          found -= 1 unless __prop_deserializer_4(hash)
          found -= 1 unless __prop_deserializer_5(hash)
          found -= 1 unless __prop_deserializer_6(hash)
          found -= 1 unless __prop_deserializer_7(hash)
          found -= 1 unless __prop_deserializer_8(hash)
          found -= 1 unless __prop_deserializer_9(hash)
          found -= 1 unless __prop_deserializer_10(hash)
          found -= 1 unless __prop_deserializer_11(hash)
          found -= 1 unless __prop_deserializer_12(hash)
          found -= 1 unless __prop_deserializer_13(hash)
          found -= 1 unless __prop_deserializer_14(hash)
          found -= 1 unless __prop_deserializer_15(hash)
          found -= 1 unless __prop_deserializer_16(hash)
          found -= 1 unless __prop_deserializer_17(hash)
          found -= 1 unless __prop_deserializer_18(hash)
          found
        end,

        19 => lambda do |hash|
          found = 20
          found -= 1 unless __prop_deserializer_0(hash)
          found -= 1 unless __prop_deserializer_1(hash)
          found -= 1 unless __prop_deserializer_2(hash)
          found -= 1 unless __prop_deserializer_3(hash)
          found -= 1 unless __prop_deserializer_4(hash)
          found -= 1 unless __prop_deserializer_5(hash)
          found -= 1 unless __prop_deserializer_6(hash)
          found -= 1 unless __prop_deserializer_7(hash)
          found -= 1 unless __prop_deserializer_8(hash)
          found -= 1 unless __prop_deserializer_9(hash)
          found -= 1 unless __prop_deserializer_10(hash)
          found -= 1 unless __prop_deserializer_11(hash)
          found -= 1 unless __prop_deserializer_12(hash)
          found -= 1 unless __prop_deserializer_13(hash)
          found -= 1 unless __prop_deserializer_14(hash)
          found -= 1 unless __prop_deserializer_15(hash)
          found -= 1 unless __prop_deserializer_16(hash)
          found -= 1 unless __prop_deserializer_17(hash)
          found -= 1 unless __prop_deserializer_18(hash)
          found -= 1 unless __prop_deserializer_19(hash)
          found
        end,
      }.freeze

      # Generated by copying the first entry into a console, then:
      #
      # (0..20).each {|i| puts "when :__prop_attr_writer_#{i}\n  #{src.gsub("\n", "\n  ").sub("0", i.to_s)}\n" }

      def self.deserialize_proc_with_transform(
        serialized_form:,
        transformer:,
        nil_handler:,
        attr_writer_name:
      )
        case attr_writer_name

        when :__prop_attr_writer_0
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_0(val)
              true
            end
          end

        when :__prop_attr_writer_1
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_1(val)
              true
            end
          end

        when :__prop_attr_writer_2
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_2(val)
              true
            end
          end

        when :__prop_attr_writer_3
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_3(val)
              true
            end
          end

        when :__prop_attr_writer_4
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_4(val)
              true
            end
          end

        when :__prop_attr_writer_5
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_5(val)
              true
            end
          end

        when :__prop_attr_writer_6
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_6(val)
              true
            end
          end

        when :__prop_attr_writer_7
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_7(val)
              true
            end
          end

        when :__prop_attr_writer_8
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_8(val)
              true
            end
          end

        when :__prop_attr_writer_9
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_9(val)
              true
            end
          end

        when :__prop_attr_writer_10
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_10(val)
              true
            end
          end

        when :__prop_attr_writer_11
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_11(val)
              true
            end
          end

        when :__prop_attr_writer_12
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_12(val)
              true
            end
          end

        when :__prop_attr_writer_13
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_13(val)
              true
            end
          end

        when :__prop_attr_writer_14
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_14(val)
              true
            end
          end

        when :__prop_attr_writer_15
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_15(val)
              true
            end
          end

        when :__prop_attr_writer_16
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_16(val)
              true
            end
          end

        when :__prop_attr_writer_17
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_17(val)
              true
            end
          end

        when :__prop_attr_writer_18
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_18(val)
              true
            end
          end

        when :__prop_attr_writer_19
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_19(val)
              true
            end
          end

        when :__prop_attr_writer_20
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              val = transformer.call(val)
              __prop_attr_writer_20(val)
              true
            end
          end
        else
          raise ArgumentError.new("Unsupported method name #{attr_writer_name}")
        end
      end

      def self.deserialize_proc_without_transform(
        serialized_form:,
        nil_handler:,
        attr_writer_name:
      )
        case attr_writer_name

        when :__prop_attr_writer_0
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_0(val)
              true
            end
          end

        when :__prop_attr_writer_1
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_1(val)
              true
            end
          end

        when :__prop_attr_writer_2
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_2(val)
              true
            end
          end

        when :__prop_attr_writer_3
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_3(val)
              true
            end
          end

        when :__prop_attr_writer_4
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_4(val)
              true
            end
          end

        when :__prop_attr_writer_5
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_5(val)
              true
            end
          end

        when :__prop_attr_writer_6
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_6(val)
              true
            end
          end

        when :__prop_attr_writer_7
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_7(val)
              true
            end
          end

        when :__prop_attr_writer_8
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_8(val)
              true
            end
          end

        when :__prop_attr_writer_9
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_9(val)
              true
            end
          end

        when :__prop_attr_writer_10
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_10(val)
              true
            end
          end

        when :__prop_attr_writer_11
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_11(val)
              true
            end
          end

        when :__prop_attr_writer_12
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_12(val)
              true
            end
          end

        when :__prop_attr_writer_13
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_13(val)
              true
            end
          end

        when :__prop_attr_writer_14
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_14(val)
              true
            end
          end

        when :__prop_attr_writer_15
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_15(val)
              true
            end
          end

        when :__prop_attr_writer_16
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_16(val)
              true
            end
          end

        when :__prop_attr_writer_17
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_17(val)
              true
            end
          end

        when :__prop_attr_writer_18
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_18(val)
              true
            end
          end

        when :__prop_attr_writer_19
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_19(val)
              true
            end
          end

        when :__prop_attr_writer_20
          lambda do |hash|
            val = hash[serialized_form]
            if val.nil?
              nil_handler&.apply(self)
              hash.key?(serialized_form)
            else
              __prop_attr_writer_20(val)
              true
            end
          end
        else
          raise ArgumentError.new("Unsupported method name #{attr_writer_name}")
        end
      end
    end
  end
end
