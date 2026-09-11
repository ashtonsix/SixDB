        // Optional large-source audit runs after the timed loop. It replays the
        // exact observed query prefix, checks the checksum and counts useful
        // logical-source 64-byte lines. This is not a hardware miss counter.
        if (std::getenv("SERIESPACK_AUDIT_LARGE") && c.random) {
            require(c.geometry == sp::geometry::local8 && c.head == 0 &&
                    (c.width == 1 || c.width == 56) && c.point_path == 0,
                    "large audit only covers selected dense Local1/56 ordinary readers");
            auto audit_cursor = initial_cursor;
            auto walk = initial_walk;
            U expected = 0;
            const auto queries = std::size_t(state.iterations()) * batch;
            const auto lines = (f.encoded_bytes + 63) / 64;
            const bool same_wire = !c.prior || c.predecessor;
            std::vector<std::uint64_t> visited(same_wire ? (lines + 63) / 64 : 0);
            std::size_t distinct = 0;
            for (std::size_t q = 0; q < queries; ++q) {
                auto index = operation == operation::dependent ? walk.index : f.indices[audit_cursor++];
                if (audit_cursor == f.indices.size()) audit_cursor = 0;
                if (operation == operation::get16) index &= ~std::size_t{15};
                const auto values = operation == operation::get16 ? 16 : 1;
                const auto value = value_at(index, c.width);
                expected += value;
                if (operation == operation::get16) expected += value_at(index + 15, c.width);
                if (operation == operation::dependent) walk.advance(value, seed, c.count);
                const auto first_line = (index * c.width / 8) / 64;
                const auto last_line = (((index + values) * c.width - 1) / 8) / 64;
                for (auto line = first_line; same_wire && line <= last_line; ++line) {
                    auto& bits = visited[line / 64];
                    const auto bit = std::uint64_t{1} << (line % 64);
                    distinct += (bits & bit) == 0;
                    bits |= bit;
                }
            }
            require(expected == checksum, "large exact-prefix checksum");
            require(operation == operation::dependent ?
                    walk.index == f.dependent.index && walk.nonce == f.dependent.nonce :
                    audit_cursor == cursor, "large exact-prefix terminal state");
            state.counters["audited_queries"] = queries;
            state.counters["logical_line_audit_applicable"] = same_wire;
            if (same_wire) state.counters["useful_source_distinct_64b_lines"] = distinct;
            state.counters["query_cursor_begin"] = initial_cursor;
            state.counters["query_cursor_end"] = cursor;
            state.counters["nonce_begin"] = initial_walk.nonce;
            state.counters["nonce_end"] = f.dependent.nonce;
            state.counters["dependent_index_begin"] = initial_walk.index;
            state.counters["dependent_index_end"] = f.dependent.index;
        }
