#!/usr/bin/env python3
"""Compile isolated complete-operation mutation experiments from one build.

The generated headers are diagnostic alternatives, not live implementation.
Every replacement is checked; the receipt records commands and source hashes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def replace(source, before, after, count=1):
    assert source.count(before) == count, (before, source.count(before), count)
    return source.replace(before, after)


def fused(source):
    source = replace(source, '        auto r=[&] {', '        auto r=[&] [[gnu::always_inline]] {')
    source = replace(source, 'index_.mutate(key,[&](const std::optional<record>& old)',
                     'index_.mutate(key,[&] [[gnu::always_inline]] (const std::optional<record>& old)', 2)
    for signature in ('std::expected<mutation_delta,mutation_error> try_set_bytes', 'mutation_delta set_bytes'):
        before = '    ' + signature + '(word key,const std::array<byte,ValueBytes>& value) {'
        source = replace(source, before, '    [[gnu::always_inline]] ' + before.strip())
    return source


def partial_promoted(source):
    source = replace(source, '        auto r=[&] [[gnu::always_inline]] {', '''        // Diagnostic: handle an existing row without making a whole-record proposal.
        auto existing=[&] [[gnu::always_inline]] (auto& edit)noexcept->std::optional<mutation_delta> {
            if constexpr(!multimap) {
                edit.template patch<ValueBytes>(0,std::span<const byte,ValueBytes>(value));
                return mutation_delta{};
            }else if constexpr(bag_policy::capacity>1) {
                const auto tag=edit.tag();
                if(tag!=1) {
                    const unsigned count=tag<2?1:tag;
                    if(count<bag_policy::capacity) {
                        edit.template patch<ValueBytes>(count*ValueBytes,std::span<const byte,ValueBytes>(value),byte(count+1));
                        return mutation_delta{0,1};
                    }
                }
            }
            return {};
        };
        auto r=[&] [[gnu::always_inline]] {''')
    source = replace(source, '},observed);', '},observed,existing);', 2)
    return source


def partial_record(source):
    source = replace(source, 'struct no_record_effects {', '''struct skip_existing_record_edit {
    template<class Edit> std::optional<mutation_delta> operator()(Edit&)const {return {};}
};
struct no_record_effects {''')
    source = replace(source, '        record read(unsigned rank)const {return view().read(rank);}', '''        record read(unsigned rank)const {return view().read(rank);}
        byte tag(unsigned rank)const {
            static_assert(Kind==container_kind::multimap);
            if constexpr(legacy)return get<1>(view().payload_at(rank)+View::payload_bytes);
            else {
                constexpr auto tag=Plan.get(layout_solver::field_id::tag);
                const auto shift=(tag.offset_bits+rank*tag.stride_bits)%8;
                return (get<1,Direct>(view().template at<layout_solver::field_id::tag>(rank))>>shift)&mask(tag.logical_bits);
            }
        }
        template<unsigned Bytes> void patch(unsigned rank,unsigned offset,std::span<const byte,Bytes> bytes,
                                           std::optional<byte> next_tag)const noexcept {
            patch_bytes<Bytes>(view().payload_at(rank)+offset,bytes);
            if constexpr(Kind==container_kind::multimap)if(next_tag) {
                if constexpr(legacy)put<1>(view().payload_at(rank)+View::payload_bytes,*next_tag);
                else {
                    constexpr auto tag=Plan.get(layout_solver::field_id::tag);
                    auto* p=view().template at<layout_solver::field_id::tag>(rank);
                    if constexpr(tag.width_bits<8) {
                        const auto shift=(tag.offset_bits+rank*tag.stride_bits)%8;
                        constexpr auto bits=mask(tag.logical_bits);
                        put<1,Direct>(p,(get<1,Direct>(p)&~(bits<<shift))|(word(*next_tag&bits)<<shift));
                    }else put<1,Direct>(p,*next_tag&mask(tag.logical_bits));
                }
            }
        }''')
    source = replace(source, '    struct location {', '''    template<unsigned Bytes,bool DirectStore=Direct> static void patch_bytes(byte* destination,std::span<const byte,Bytes> bytes)noexcept {
        lanes<Bytes/8>([&](auto i){word v;std::memcpy(&v,bytes.data()+8*i,8);put<8,DirectStore>(destination+8*i,v);});
        if constexpr(Bytes%8){word v=0;std::memcpy(&v,bytes.data()+Bytes/8*8,Bytes%8);put<Bytes%8,DirectStore>(destination+Bytes/8*8,v);}
    }
    struct location {''')
    source = replace(source, '  public:\n    explicit record_table', '''    // Scoped, nonreentrant existing-row continuation. It must not escape or
    // mutate/relocate the index. A declined callback has made no writes, events
    // or owner changes. A successful callback makes at most one patch and returns
    // keys==0. Patch bytes must fit the payload and remain stable for this
    // infallible commit; all validation precedes the first observer event.
    // Parent bucket/block damage is published before any packed field changes.
    template<class Effects> class record_edit {
        record_table& owner_;location at_;Effects& effects_;
      public:
        record_edit(record_table& owner,location at,Effects& effects):owner_(owner),at_(at),effects_(effects){}
        byte tag()const {
            if(at_.tier==1)return owner_.p(at_.index).tag(at_.rank);
            if(at_.tier==2)return owner_.q(at_.index).tag(at_.rank);
            return get<1>(owner_.overflow_.payload(at_.link).data()+primary_view::payload_bytes);
        }
        template<unsigned Bytes> void patch(unsigned offset,std::span<const byte,Bytes> bytes,
                                           std::optional<byte> tag={})noexcept {
            static_assert(Bytes<=primary_view::payload_bytes);
            if constexpr(Bytes || Kind==container_kind::multimap) {
                if(at_.tier==1){owner_.before_p(at_.index,effects_);owner_.p(at_.index).template patch<Bytes>(at_.rank,offset,bytes,tag);}
                else if(at_.tier==2){owner_.before_q(at_.index,effects_);owner_.q(at_.index).template patch<Bytes>(at_.rank,offset,bytes,tag);}
                else {
                    owner_.overflow_.observe_payload(at_.link,effects_);
                    auto* p=owner_.overflow_.payload(at_.link).data();patch_bytes<Bytes,false>(p+offset,bytes);
                    if constexpr(Kind==container_kind::multimap)if(tag)put<1>(p+primary_view::payload_bytes,*tag);
                }
            }
        }
    };
  public:
    explicit record_table''')
    source = replace(source, '''    template<class Prepare,class Effects> [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    mutate(word key,Prepare&& prepare,Effects& effects) {''', '''    template<class Prepare,class Effects,class Existing=skip_existing_record_edit>
    [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    mutate(word key,Prepare&& prepare,Effects& effects,Existing existing={}) {''')
    source = replace(source, '''        auto h=hp_.split(key);auto at=find(key,h);std::optional<record> old;
        if(at.tier)old=read(at);''', '''        auto h=hp_.split(key);auto at=find(key,h);std::optional<record> old;
        if(at.tier) {
            record_edit<Effects> edit(*this,at,effects);
            if(auto done=existing(edit))return *done;
            old=read(at);
        }''')
    return source


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('build', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--source-root', type=Path,
                        help='spike directory from the pre-adoption source capture; benchmark/check TUs still come from the current spike')
    parser.add_argument('--widths', type=int, nargs='*', default=[2])
    parser.add_argument('--variants', nargs='+', choices=['control', 'fused', 'partial', 'nonzero'], default=['control', 'fused', 'partial'])
    parser.add_argument('--checks', action='store_true')
    parser.add_argument('--check-widths', type=int, nargs='*', default=[])
    parser.add_argument('--check-variants', nargs='+', default=['control', 'fused', 'partial'])
    parser.add_argument('--extra-check', action='append', default=[])
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    spike = Path(__file__).resolve().parent
    build, out = args.build.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    source_root = args.source_root.resolve() if args.source_root else spike
    original = (source_root / 'promoted.h').read_text()
    if 'auto existing=' in original:
        parser.error('mutation paths are already adopted; supply --source-root from the pre-adoption capture to reproduce these controls')
    headers = {p.name: sha(p) for p in spike.glob('*.h')}
    entries = json.loads((build / 'compile_commands.json').read_text())
    receipt = {'note': 'isolated complete-operation mutation probes; same linked libraries; live headers unchanged',
               'header_sha256': headers, 'commands': []}
    for variant in args.variants:
        path = out / variant
        path.mkdir(exist_ok=True)
        source = original if variant == 'control' else fused(original)
        if variant in ('partial', 'nonzero'):
            source = partial_promoted(source)
            (path / 'record_table.h').write_text(partial_record((source_root / 'record_table.h').read_text()))
        else:
            (path / 'record_table.h').write_bytes((source_root / 'record_table.h').read_bytes())
        if variant == 'nonzero':
            source = replace(source, 'if(r){owner_.keys+=r->keys;owner_.values+=r->values;',
                             'if(r){if(r->keys)owner_.keys+=r->keys;if(r->values)owner_.values+=r->values;')
        (path / 'promoted.h').write_text(source)
        tasks = [(width, 'bench', f'hashtable_design_value_{width}_bench') for width in args.widths]
        if variant in args.check_variants:
            check_widths = set(args.check_widths) | (set(args.widths) if args.checks else set())
            tasks += [(width, 'check', f'hashtable_design_value_{width}_check') for width in sorted(check_widths)]
            tasks += [(0, name, name) for name in args.extra_check]
        for width, kind, target in tasks:
            entry = next(e for e in entries if f'{target}.dir' in e['command'])
            base = shlex.split(entry['command'])
            old_object, old_source = base[base.index('-o')+1], base[base.index('-c')+1]
            unit = path / Path(old_source).name
            unit.write_bytes((spike / unit.name).read_bytes())
            obj = path / f'value-{width}-{kind}.o'
            exe = path / (f'values-{width}{"-check" if kind == "check" else ""}' if width else kind)
            raw = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()[-1]
            assert raw.startswith(': && ') and raw.endswith(' && :')
            link = shlex.split(raw[5:-5])
            cmd, i = [], 0
            while i < len(base):
                arg = base[i]
                if arg in ('-MD', '-MMD'): i += 1; continue
                if arg in ('-MT', '-MF'): i += 2; continue
                cmd.append(str(obj) if arg == old_object else str(unit) if arg == old_source else arg)
                i += 1
            cmd += ['-I'+str(spike)]
            linked, i = [], 0
            while i < len(link):
                arg = link[i]
                if arg == '-Xlinker' and link[i+1].startswith('--dependency-file='): i += 2; continue
                linked.append(str(obj) if arg == old_object else arg)
                i += 1
            linked[linked.index('-o')+1] = str(exe)
            item = {'variant': variant, 'width': width, 'kind': kind, 'source_sha256': sha(unit),
                    'promoted_sha256': sha(path / 'promoted.h'), 'compile': cmd, 'link': linked}
            item['record_table_sha256'] = sha(path / 'record_table.h')
            receipt['commands'].append(item)
            (out / 'receipt.json').write_text(json.dumps(receipt, indent=2))
            with (path / f'compile-{width}-{kind}.log').open('w') as log:
                subprocess.run(cmd, cwd=entry['directory'], stdout=log, stderr=subprocess.STDOUT, check=True)
            with (path / f'link-{width}-{kind}.log').open('w') as log:
                subprocess.run(linked, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
            item['binary_sha256'] = sha(exe)
            (out / 'receipt.json').write_text(json.dumps(receipt, indent=2))
            print(variant, width, kind, 'built', flush=True)
    assert headers == {p.name: sha(p) for p in spike.glob('*.h')}


if __name__ == '__main__':
    main()
