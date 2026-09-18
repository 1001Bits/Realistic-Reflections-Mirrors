// Included inside MirrorSunShadowShader; reuses the validated DXBC container
// reader and checksum writer. No shader assembly round-trip or recompilation.
namespace {
struct CasterRegister { unsigned index{}, mask{}; };
struct CasterOperand { unsigned type{}, index{}, mask{}; };
CasterOperand ReadCasterOperand(const Words& words, std::size_t& p,
                               std::vector<CasterRegister>& reads,
                               bool destination=false, unsigned depth=0) {
    Require(p<words.size() && depth<8,"caster operand nesting");
    const auto token=words[p++];auto extension=token;unsigned extensions=0;
    while(extension&0x80000000u) {
        Require(p<words.size() && ++extensions<8,"caster operand extension");
        extension=words[p++];
    }
    const unsigned type=(token>>12)&0xff, dimensions=(token>>20)&3;
    std::array<unsigned,3> indices{},representation{};
    for(unsigned d=0;d<dimensions;++d) {
        const auto rep=(token>>(22+3*d))&7;representation[d]=rep;
        Require(rep==0 || rep==2 || rep==3,"caster index representation");
        if(rep==0 || rep==3){Require(p<words.size(),"caster index bounds");indices[d]=words[p++];}
        if(rep>=2)ReadCasterOperand(words,p,reads,false,depth+1);
    }
    if(type==D3D10_SB_OPERAND_TYPE_IMMEDIATE32) {
        const auto n=(token&3)==1?1u:(token&3)==2?4u:0u;
        Require(!destination && !dimensions && n && n<=words.size()-p,"caster immediate");
        p+=n;return {type};
    }
    unsigned mask=0;
    if(type==D3D10_SB_OPERAND_TYPE_TEMP || type==D3D10_SB_OPERAND_TYPE_OUTPUT) {
        Require(dimensions==1 && representation[0]==0 && (token&3)==2,"caster vector register");
        const auto selection=(token>>2)&3;
        if(selection==0)mask=(token>>4)&15;
        else if(selection==1)for(unsigned n=0;n<4;++n)mask|=1u<<((token>>(4+2*n))&3);
        else if(selection==2)mask=1u<<((token>>4)&3);
        Require(mask && (!destination || selection==0),"caster component selection");
        if(type==D3D10_SB_OPERAND_TYPE_TEMP) {
            Require(indices[0]<4096,"caster temp index");
            if(!destination)reads.push_back({indices[0],mask});
        } else Require(destination && indices[0]<8,"caster output read/index");
    } else {
        Require(!destination,"caster special destination");
        switch(type) {
        case D3D10_SB_OPERAND_TYPE_INPUT:
            Require(dimensions==1 && representation[0]==0 && indices[0]<32,"caster input");break;
        case D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER:
            Require(dimensions==2 && representation[0]==0 && indices[0]<14,"caster constant buffer");break;
        case D3D10_SB_OPERAND_TYPE_RESOURCE:
            Require(dimensions==1 && representation[0]==0 && indices[0]<128,"caster resource");break;
        case D3D10_SB_OPERAND_TYPE_SAMPLER:
            Require(dimensions==1 && representation[0]==0 && indices[0]<16,"caster sampler");break;
        default:Require(false,"caster unsupported operand");
        }
    }
    return {type,indices[0],mask};
}
bool CasterSingleOutput(unsigned op) noexcept {
    switch(op) {
    case D3D10_SB_OPCODE_ADD: case D3D10_SB_OPCODE_AND: case D3D10_SB_OPCODE_DIV:
    case D3D10_SB_OPCODE_DP2: case D3D10_SB_OPCODE_DP3: case D3D10_SB_OPCODE_DP4:
    case D3D10_SB_OPCODE_EQ: case D3D10_SB_OPCODE_EXP: case D3D10_SB_OPCODE_FRC:
    case D3D10_SB_OPCODE_FTOI: case D3D10_SB_OPCODE_FTOU: case D3D10_SB_OPCODE_GE:
    case D3D10_SB_OPCODE_DERIV_RTX: case D3D10_SB_OPCODE_DERIV_RTY:
    case D3D10_SB_OPCODE_IADD: case D3D10_SB_OPCODE_IEQ: case D3D10_SB_OPCODE_IGE:
    case D3D10_SB_OPCODE_ILT: case D3D10_SB_OPCODE_IMAD: case D3D10_SB_OPCODE_IMAX:
    case D3D10_SB_OPCODE_IMIN: case D3D10_SB_OPCODE_INE: case D3D10_SB_OPCODE_INEG:
    case D3D10_SB_OPCODE_ISHL: case D3D10_SB_OPCODE_ISHR: case D3D10_SB_OPCODE_ITOF:
    case D3D10_SB_OPCODE_LD: case D3D10_SB_OPCODE_LD_MS: case D3D10_SB_OPCODE_LOG:
    case D3D10_SB_OPCODE_LT: case D3D10_SB_OPCODE_MAD: case D3D10_SB_OPCODE_MAX:
    case D3D10_SB_OPCODE_MIN: case D3D10_SB_OPCODE_MOV: case D3D10_SB_OPCODE_MOVC:
    case D3D10_SB_OPCODE_MUL: case D3D10_SB_OPCODE_NE: case D3D10_SB_OPCODE_NOT:
    case D3D10_SB_OPCODE_OR: case D3D10_SB_OPCODE_RESINFO: case D3D10_SB_OPCODE_ROUND_NE:
    case D3D10_SB_OPCODE_ROUND_NI: case D3D10_SB_OPCODE_ROUND_PI: case D3D10_SB_OPCODE_ROUND_Z:
    case D3D10_SB_OPCODE_RSQ: case D3D10_SB_OPCODE_SAMPLE: case D3D10_SB_OPCODE_SAMPLE_B:
    case D3D10_SB_OPCODE_SAMPLE_L: case D3D10_SB_OPCODE_SAMPLE_D: case D3D10_SB_OPCODE_SAMPLE_C:
    case D3D10_SB_OPCODE_SAMPLE_C_LZ: case D3D10_SB_OPCODE_SQRT: case D3D10_SB_OPCODE_ULT:
    case D3D10_SB_OPCODE_UGE: case D3D10_SB_OPCODE_UMAX: case D3D10_SB_OPCODE_UMIN:
    case D3D10_SB_OPCODE_UMAD: case D3D10_SB_OPCODE_USHR: case D3D10_SB_OPCODE_UTOF:
    case D3D10_SB_OPCODE_XOR: case D3D10_1_SB_OPCODE_LOD: case D3D10_1_SB_OPCODE_GATHER4:
    case D3D11_SB_OPCODE_BUFINFO: case D3D11_SB_OPCODE_DERIV_RTX_COARSE:
    case D3D11_SB_OPCODE_DERIV_RTX_FINE: case D3D11_SB_OPCODE_DERIV_RTY_COARSE:
    case D3D11_SB_OPCODE_DERIV_RTY_FINE: case D3D11_SB_OPCODE_GATHER4_C:
    case D3D11_SB_OPCODE_GATHER4_PO: case D3D11_SB_OPCODE_GATHER4_PO_C:
    case D3D11_SB_OPCODE_RCP: case D3D11_SB_OPCODE_F32TOF16: case D3D11_SB_OPCODE_F16TOF32:
    case D3D11_SB_OPCODE_COUNTBITS: case D3D11_SB_OPCODE_FIRSTBIT_HI:
    case D3D11_SB_OPCODE_FIRSTBIT_LO: case D3D11_SB_OPCODE_FIRSTBIT_SHI:
    case D3D11_SB_OPCODE_UBFE: case D3D11_SB_OPCODE_IBFE: case D3D11_SB_OPCODE_BFI:
    case D3D11_SB_OPCODE_BFREV: case D3D11_SB_OPCODE_LD_RAW: case D3D11_SB_OPCODE_LD_STRUCTURED:
        return true;
    default:return false;
    }
}
struct CasterFact {
    unsigned depth{};
    CasterOperand destination{};
    std::vector<CasterRegister> reads;
    bool declaration{}, discard{}, ordinary{}, selected{};
};
} // namespace

CasterVariant BuildCaster(std::span<const std::uint8_t> original) {
    CasterVariant result;
    try {
        auto chunks=Chunks(original);const auto instructions=Instructions(Code(chunks));
        Require(instructions.size()<=4096,"caster program size");
        ComPtr<ID3D11ShaderReflection> reflection;
        Require(SUCCEEDED(D3DReflect(original.data(),original.size(),IID_PPV_ARGS(&reflection))),"caster reflection");
        D3D11_SHADER_DESC description{};Require(SUCCEEDED(reflection->GetDesc(&description)),"caster description");
        Require(description.OutputParameters<=8,"caster outputs");
        for(unsigned n=0;n<description.OutputParameters;++n) {
            D3D11_SIGNATURE_PARAMETER_DESC output{};
            Require(SUCCEEDED(reflection->GetOutputParameterDesc(n,&output)) && output.SystemValueType==D3D_NAME_TARGET,
                    "caster depth/coverage output");
        }
        std::vector<CasterFact> facts(instructions.size());
        struct Flow {unsigned op;bool alternative{};};std::vector<Flow> flow;
        bool body=false;unsigned discards=0;
        for(std::size_t n=0;n<instructions.size();++n) {
            const auto& i=instructions[n];auto& f=facts[n];f.depth=static_cast<unsigned>(flow.size());
            std::size_t p=1;auto extension=i.words[0];unsigned extensions=0;
            while(extension&0x80000000u){Require(p<i.words.size() && ++extensions<8,"caster instruction extension");extension=i.words[p++];}
            if(Declaration(i.opcode)) {
                Require(!body,"caster late declaration");f.declaration=true;
                switch(i.opcode) {
                case D3D10_SB_OPCODE_DCL_OUTPUT:
                    Require(ReadCasterOperand(i.words,p,f.reads,true).type==D3D10_SB_OPERAND_TYPE_OUTPUT && p==i.words.size(),"caster output declaration");break;
                case D3D10_SB_OPCODE_DCL_RESOURCE: case D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER:
                case D3D10_SB_OPCODE_DCL_SAMPLER: case D3D10_SB_OPCODE_DCL_INPUT_PS:
                case D3D10_SB_OPCODE_DCL_INPUT_PS_SGV: case D3D10_SB_OPCODE_DCL_INPUT_PS_SIV:
                case D3D10_SB_OPCODE_DCL_TEMPS: case D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS:
                case D3D11_SB_OPCODE_DCL_RESOURCE_RAW: case D3D11_SB_OPCODE_DCL_RESOURCE_STRUCTURED:break;
                default:Require(false,"caster unsupported declaration");
                }
                continue;
            }
            body=true;++result.originalInstructions;
            const auto operands=OperandCount(i.opcode);
            f.ordinary=CasterSingleOutput(i.opcode);f.discard=i.opcode==D3D10_SB_OPCODE_DISCARD;
            for(unsigned operand=0;operand<operands;++operand) {
                const auto value=ReadCasterOperand(i.words,p,f.reads,f.ordinary && operand==0);
                if(f.ordinary && operand==0)f.destination=value;
            }
            Require(p==i.words.size(),"caster instruction payload");
            if(f.ordinary)continue;
            switch(i.opcode) {
            case D3D10_SB_OPCODE_IF: case D3D10_SB_OPCODE_LOOP: case D3D10_SB_OPCODE_SWITCH:
                Require(flow.size()<32,"caster flow nesting");flow.push_back({i.opcode});break;
            case D3D10_SB_OPCODE_ELSE:
                Require(!flow.empty() && flow.back().op==D3D10_SB_OPCODE_IF && !flow.back().alternative,"caster else");flow.back().alternative=true;break;
            case D3D10_SB_OPCODE_ENDIF: case D3D10_SB_OPCODE_ENDLOOP: case D3D10_SB_OPCODE_ENDSWITCH:
                Require(!flow.empty() && flow.back().op==static_cast<unsigned>(i.opcode==D3D10_SB_OPCODE_ENDIF?D3D10_SB_OPCODE_IF:
                    i.opcode==D3D10_SB_OPCODE_ENDLOOP?D3D10_SB_OPCODE_LOOP:D3D10_SB_OPCODE_SWITCH),"caster flow close");flow.pop_back();break;
            case D3D10_SB_OPCODE_BREAK: case D3D10_SB_OPCODE_BREAKC:
                Require(std::any_of(flow.begin(),flow.end(),[](const Flow& v){return v.op==D3D10_SB_OPCODE_LOOP || v.op==D3D10_SB_OPCODE_SWITCH;}),"caster break");break;
            case D3D10_SB_OPCODE_CONTINUE: case D3D10_SB_OPCODE_CONTINUEC:
                Require(std::any_of(flow.begin(),flow.end(),[](const Flow& v){return v.op==D3D10_SB_OPCODE_LOOP;}),"caster continue");break;
            case D3D10_SB_OPCODE_CASE: case D3D10_SB_OPCODE_DEFAULT:
                Require(!flow.empty() && flow.back().op==D3D10_SB_OPCODE_SWITCH,"caster switch label");break;
            case D3D10_SB_OPCODE_RET:
                Require(flow.empty() && n+1==instructions.size(),"caster early return");f.selected=true;break;
            case D3D10_SB_OPCODE_DISCARD:Require(flow.empty(),"caster conditional discard");++discards;break;
            case D3D10_SB_OPCODE_NOP:break;
            default:Require(false,"caster side effect or unsupported opcode");
            }
        }
        Require(flow.empty() && !instructions.empty() && instructions.back().opcode==D3D10_SB_OPCODE_RET && discards,"caster incomplete alpha program");
        std::array<unsigned char,4096> live{};
        for(std::size_t n=instructions.size();n--;) {
            auto& f=facts[n];if(f.declaration)continue;
            const auto& d=f.destination;
            if(f.ordinary && d.type==D3D10_SB_OPERAND_TYPE_TEMP && (live[d.index]&d.mask)) {
                Require(!f.depth,"caster conditional alpha dependency");
                f.selected=true;live[d.index]&=static_cast<unsigned char>(~d.mask);
            }
            if(f.discard)f.selected=true;
            if(f.selected)for(const auto& r:f.reads)live[r.index]|=static_cast<unsigned char>(r.mask);
        }
        Require(std::none_of(live.begin(),live.end(),[](unsigned char m){return m!=0;}),"caster undefined alpha input");
        Words declarations,kept;Rewrite inspection{};inspection.lightSlot=~0u;
        for(std::size_t n=0;n<instructions.size();++n) {
            const auto& i=instructions[n];const auto& f=facts[n];
            if(f.declaration) {
                if(i.opcode!=D3D10_SB_OPCODE_DCL_OUTPUT && i.opcode!=D3D10_SB_OPCODE_DCL_TEMPS)
                    declarations.insert(declarations.end(),i.words.begin(),i.words.end());
            } else if(f.selected) {
                Require(Recode(i,inspection)==i.words,"caster alpha recoding");
                kept.insert(kept.end(),i.words.begin(),i.words.end());++result.retainedInstructions;
            }
        }
        if(inspection.maxTemp){declarations.push_back((2u<<24)|D3D10_SB_OPCODE_DCL_TEMPS);declarations.push_back(inspection.maxTemp);}
        Words code{0x50,static_cast<unsigned>(2+declarations.size()+kept.size())};
        code.insert(code.end(),declarations.begin(),declarations.end());code.insert(code.end(),kept.begin(),kept.end());
        std::vector<Chunk> output;unsigned signatures=0;
        for(auto& c:chunks) {
            if(c.tag==Tag('S','H','E','X') || c.tag==Tag('S','H','D','R')){c.data.resize(code.size()*4);std::memcpy(c.data.data(),code.data(),c.data.size());}
            else if(c.tag==Tag('O','S','G','N') || c.tag==Tag('O','S','G','1')){++signatures;c.tag=Tag('O','S','G','N');c.data.assign(8,0);Put(c.data,4,8);}
            else if(c.tag!=Tag('I','S','G','N') && c.tag!=Tag('I','S','G','1') && c.tag!=Tag('S','F','I','0'))continue;
            output.push_back(std::move(c));
        }
        Require(signatures==1,"caster output signature");
        auto& bytes=result.bytecode;bytes.resize(32+4*output.size());Put(bytes,0,Tag('D','X','B','C'));Put(bytes,20,1);Put(bytes,28,static_cast<unsigned>(output.size()));
        for(unsigned n=0;n<output.size();++n) {
            const auto offset=bytes.size();const auto& c=output[n];Put(bytes,32+4*n,static_cast<unsigned>(offset));bytes.resize(offset+8+c.data.size());
            Put(bytes,offset,c.tag);Put(bytes,offset+4,static_cast<unsigned>(c.data.size()));std::memcpy(bytes.data()+offset+8,c.data.data(),c.data.size());
        }
        Put(bytes,24,static_cast<unsigned>(bytes.size()));MirrorSunShadowContainerHash::ComputeHashRetail(bytes.data()+20,static_cast<unsigned>(bytes.size()-20),bytes.data()+4);
        result.reason="ready";
    } catch(const std::exception& e){result.bytecode.clear();result.reason=e.what();}
    return result;
}
