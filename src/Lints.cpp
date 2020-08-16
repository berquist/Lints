#include "jlcxx/jlcxx.hpp"
#include "jlcxx/tuple.hpp"
#include "libint2.hpp"
#include <fstream>
#include <string>
#include <vector>
#include <iterator>
#include <math.h>

void libint2_init()
{
    libint2::initialize();
    libint2::Engine eri2(libint2::Operator::coulomb, 2, 2);
    eri2.set(libint2::BraKet::xs_xx);
}

void libint2_finalize()
{
    libint2::finalize();
}

int nharms(int n)
{
    switch(n) {
        case 0:
            return 1;
            break;
        case 1:
            return 3;
            break;
        case 2:
            return 6;
            break;
        default:
            return n*3 + nharms(n-3);
    }
}


std::vector<libint2::Atom> get_atoms(std::string& inp)
{
    std::ifstream input_file(inp);
    std::vector<libint2::Atom> atoms = libint2::read_dotxyz(input_file);
    return atoms;
}

libint2::Engine make_engine(libint2::Operator op, int max_nprim, int max_l)
{
    libint2::Engine engine(op,max_nprim,max_l);
    return engine;
}

struct Atom
{
    Atom(int Z, double x, double y, double z) {
        libint2::Atom atom;
        atom.atomic_number = Z;
        atom.x = x;
        atom.y = y;
        atom.z = z;
        this->atom = atom;
    }
    Atom(libint2::Atom& atom) {
        this->atom = atom;
    }
    libint2::Atom atom;
    int Z() { return this->atom.atomic_number; }
    auto get_pos() { 
        jlcxx::Array<double> pos;
        pos.push_back(this->atom.x);
        pos.push_back(this->atom.y);
        pos.push_back(this->atom.z);
        return pos;
    }
    auto set_pos(double x, double y, double z) {
        this->atom.x = x;
        this->atom.y = y;
        this->atom.z = z;
    }
};

struct Molecule
{
    Molecule(jlcxx::ArrayRef<long> zs, jlcxx::ArrayRef<double> coords) {
        std::vector<libint2::Atom> _atoms;
        this->len = zs.size();
        double x, y, z;
        for (int i=0, j=0; i < this->len; i++, j += 3) {
            libint2::Atom a;
            a.atomic_number = zs[i];
            a.x = coords[j];
            a.y = coords[j+1];
            a.z = coords[j+2];
            _atoms.push_back(a);
        }
        this->atoms = _atoms;
    } 
    Molecule(std::string& inp) {
        this->atoms = get_atoms(inp);
        this->len = this->atoms.size();
    }
    auto push_back(int Z, double x, double y, double z) {
        libint2::Atom atom;//(Z,x,y,z);
        atom.atomic_number = Z;
        atom.x = x;
        atom.y = y;
        atom.z = z;
        this->atoms.push_back(atom);
        this->Atoms.push_back(Atom(atom));
        this->len++;
    }
    auto get_Atom(int i) {
        return Atom(this->atoms[i]);
    }
    auto get_Atoms() {
        return this->Atoms;
    }
    auto get_size() {
        return this->len;
    }
    int len;
    jlcxx::Array<Atom> Atoms;
    std::vector<libint2::Atom> atoms;
};

struct BasisSet
{
    //BasisSet(std::string bname, std::string fname) { 
    //    this->basis = make_basis(bname, fname);
    //}
    BasisSet(std::string bname, std::vector<libint2::Atom> atoms) {
        libint2::BasisSet bas(bname, atoms, true);
        this->basis = bas;
    }
    BasisSet(std::string bname, Molecule mol) {
        libint2::BasisSet bas(bname,mol.atoms, true);
        this->basis = bas;
    }
    libint2::BasisSet basis;
    auto print_out() {
        std::copy(begin(this->basis),end(this->basis),
                  std::ostream_iterator<libint2::Shell>(std::cout, "\n"));
    }
    auto getsize() {
        return this->basis.size();
    }
    auto nao() {
        int d1 = 0;
        for(int n1=0; n1<this->basis.size(); n1++) {
            d1 += this->basis[n1].size();
        }
        return d1;
    }
    int max_nprim() {
        return this->basis.max_nprim();
    }
    int max_l() {
        return this->basis.max_l();
    }
    void set_pure(bool tf) {
        this->basis.set_pure(tf);
    }
};

struct OEIEngine
{
    ~OEIEngine() {
        delete[] this->_data;
    }
    libint2::Engine engine;
    int sz;
    double* _data;
    auto chunk(int s1, int s2, BasisSet& obs1, BasisSet& obs2) {
        jlcxx::Array<int> dims;
        dims.push_back(obs1.basis[s1].size());
        dims.push_back(obs2.basis[s2].size());
        return dims;
    }
    auto startpoint(int s1, int s2, BasisSet& obs1, BasisSet& obs2) {
        auto shell2bf1 = obs1.basis.shell2bf();
        auto shell2bf2 = obs2.basis.shell2bf();
        jlcxx::Array<int> coords;
        coords.push_back(shell2bf1[s1]);
        coords.push_back(shell2bf2[s2]);
        return coords;
    }
    auto data() {
        return jlcxx::make_julia_array(this->_data,this->sz);
    }
    virtual void compute(int s1, int s2, BasisSet& obs1, BasisSet& obs2) {
        const auto& buf_vec = this->engine.results();
        this->engine.compute(obs1.basis[s1],obs2.basis[s2]);
        auto n1 = obs1.basis[s1].size();
        auto n2 = obs2.basis[s2].size();
        auto ints_shellset = buf_vec[0];
        memcpy(this->_data,ints_shellset,n2*n1*sizeof(double));
        this->sz = n1*n2;
    }
};
struct OverlapEngine : OEIEngine
{
    OverlapEngine(int max_nprim, int max_l) {
        this->engine = make_engine(libint2::Operator::overlap,max_nprim,max_l);
        int maxl = nharms(max_l);
        this->_data = new double[maxl*maxl];
    }
};

struct DipoleEngine : OEIEngine
{
    DipoleEngine(int max_nprim, int max_l, double x=0.0, double y=0.0, double z=0.0) {
        this->engine = make_engine(libint2::Operator::emultipole1,max_nprim,max_l);
        std::array<double, 3> ctr = {x, y, z};
        this->engine.set_params(ctr);
        int maxl = nharms(max_l);
        this->_data = new double[maxl*maxl];
    }
    double* _mux;
    double* _muy;
    double* _muz;

    void compute(int s1, int s2, BasisSet& obs1, BasisSet& obs2) {
        const auto& buf_vec = this->engine.results();
        this->engine.compute(obs1.basis[s1],obs2.basis[s2]);
        auto n1 = obs1.basis[s1].size();
        auto n2 = obs2.basis[s2].size();
        auto s_shellset = buf_vec[0];
        auto mux_shellset = buf_vec[1];
        auto muy_shellset = buf_vec[2];
        auto muz_shellset = buf_vec[3];
        if (s_shellset == nullptr) {
            double z = 0.0;
            memset(this->_mux,z,n1*n2*sizeof(double));
            memset(this->_muy,z,n1*n2*sizeof(double));
            memset(this->_muz,z,n1*n2*sizeof(double));
        }
        else {
            memcpy(this->_mux,mux_shellset,n1*n2*sizeof(double));
            memcpy(this->_muy,muy_shellset,n1*n2*sizeof(double));
            memcpy(this->_muz,muz_shellset,n1*n2*sizeof(double));
        }
        this->sz = n1*n2;
    }
    auto recenter(double x=0.0, double y=0.0, double z=0.0) {
        std::array<double, 3> ctr = {x, y, z};
        this->engine.set_params(ctr);
    }
    auto mux() {
        return this->_mux;
    }
    auto muy() {
        return this->_muy;
    }
    auto muz() {
        return this->_muz;
    }
};
struct KineticEngine : OEIEngine
{
    KineticEngine(int max_nprim, int max_l) {
        this->engine = make_engine(libint2::Operator::kinetic,max_nprim,max_l);
        int maxl = nharms(max_l);
        this->_data = new double[maxl*maxl];
    }
};
struct NuclearEngine : OEIEngine
{
    NuclearEngine(int max_nprim, int max_l, Molecule mol) {
        libint2::Engine engine(libint2::Operator::nuclear, max_nprim, max_l);
        engine.set_params(libint2::make_point_charges(mol.atoms));
        this->engine = engine;
        int maxl = nharms(max_l);
        this->_data = new double[maxl*maxl];
    }
};
struct ERIEngine
{
    ERIEngine(int max_nprim, int max_l) {
        this->engine = make_engine(libint2::Operator::coulomb,
                                    max_nprim,
                                    max_l);
        int maxl = nharms(max_l);
        this->_data = new double[maxl*maxl*maxl*maxl];
    }
    ~ERIEngine() {
        delete[] this->_data;
    }
    libint2::Engine engine;
    int sz;
    double* _data;
    void compute(int s1, int s2, int s3, int s4, BasisSet& obs) {
        const auto& buf_vec = this->engine.results();
        this->engine.compute(obs.basis[s1],obs.basis[s2],obs.basis[s3],obs.basis[s4]);
        auto n1 = obs.basis[s1].size();
        auto n2 = obs.basis[s2].size();
        auto n3 = obs.basis[s3].size();
        auto n4 = obs.basis[s4].size();
        auto ints_shellset = buf_vec[0];
        if (ints_shellset == nullptr) {
            double z = 0.0;
            memset(this->_data,z,n1*n2*n3*n4*sizeof(double));
        }
        else {
            memcpy(this->_data,ints_shellset,n1*n2*n3*n4*sizeof(double));
        }
        this->sz = n1*n2*n3*n4;

    }
    auto data() {
        return jlcxx::make_julia_array(this->_data,this->sz);
    }
    auto chunk(int s1, int s2, int s3, int s4, BasisSet& obs) {
        jlcxx::Array<int> dims;
        dims.push_back(obs.basis[s1].size());
        dims.push_back(obs.basis[s2].size());
        dims.push_back(obs.basis[s3].size());
        dims.push_back(obs.basis[s4].size());
        return dims;
    }
    auto startpoint(int s1, int s2, int s3, int s4, BasisSet& obs) {
        auto shell2bf = obs.basis.shell2bf();
        jlcxx::Array<int> coords;
        coords.push_back(shell2bf[s1]);
        coords.push_back(shell2bf[s2]);
        coords.push_back(shell2bf[s3]);
        coords.push_back(shell2bf[s4]);
        return coords;
    }
};
struct DFEngine
{
    DFEngine(int max_nprim, int max_l) {
        this->bengine = make_engine(libint2::Operator::coulomb,
                                    max_nprim,
                                    max_l);
        this->jengine = make_engine(libint2::Operator::coulomb,
                                    max_nprim,
                                    max_l);
        this->bengine.set(libint2::BraKet::xs_xx);
        this->jengine.set(libint2::BraKet::xs_xs);
        int maxl = nharms(max_l);
        this->_bdata = new double[maxl*maxl*maxl];
        this->_jdata = new double[maxl*maxl];
    }
    double* _bdata;
    double* _jdata;
    int bsz;
    int jsz;
    libint2::Engine bengine;
    libint2::Engine jengine;
    auto compute_b(int s3, int s2, int s1, BasisSet& obs, BasisSet& dfobs) {
        const auto& buf_vec = this->bengine.results();
        this->bengine.compute(dfobs.basis[s3],obs.basis[s2],obs.basis[s1]);
        auto n1 = obs.basis[s1].size();
        auto n2 = obs.basis[s2].size();
        auto n3 = dfobs.basis[s3].size();
        auto ints_shellset = buf_vec[0];
        if (ints_shellset == nullptr) {
            double z = 0.0;
            memset(this->_bdata,z,n1*n2*n3*sizeof(double));
        }
        else {
            memcpy(this->_bdata,ints_shellset,n1*n2*n3*sizeof(double));
        }
        this->bsz = n1*n2*n3;
    }
    auto compute_j(int s1, int s2, BasisSet& dfobs) {
        const auto& buf_vec = this->jengine.results();
        this->jengine.compute(dfobs.basis[s1],dfobs.basis[s2]);
        auto n1 = dfobs.basis[s1].size();
        auto n2 = dfobs.basis[s2].size();
        auto ints_shellset = buf_vec[0];
        jlcxx::Array<double> ints;
        if (ints_shellset == nullptr) {
            double z = 0.0;
            memset(this->_jdata,z,n1*n2*sizeof(double));
        }
        else {
            memcpy(this->_jdata,ints_shellset,n1*n2*sizeof(double));
        }
        this->jsz = n1*n2;
    }
    auto bchunk(int s3, int s2, int s1, BasisSet& obs, BasisSet& dfobs) {
        jlcxx::Array<int> dims;
        dims.push_back(dfobs.basis[s3].size());
        dims.push_back(obs.basis[s2].size());
        dims.push_back(obs.basis[s1].size());
        return dims;
    }
    auto jchunk(int s1, int s2, BasisSet& dfobs) {
        jlcxx::Array<int> dims;
        dims.push_back(dfobs.basis[s1].size());
        dims.push_back(dfobs.basis[s2].size());
        return dims;
    }
    auto bstartpoint(int s3, int s2, int s1, BasisSet& obs, BasisSet& dfobs) {
        auto obs_shell2bf = obs.basis.shell2bf();
        auto dfobs_shell2bf = dfobs.basis.shell2bf();
        jlcxx::Array<int> coords;
        coords.push_back(dfobs_shell2bf[s3]);
        coords.push_back(obs_shell2bf[s2]);
        coords.push_back(obs_shell2bf[s1]);
        return coords;
    }
    auto jstartpoint(int s1, int s2, BasisSet& dfobs) {
        auto dfobs_shell2bf = dfobs.basis.shell2bf();
        jlcxx::Array<int> coords;
        coords.push_back(dfobs_shell2bf[s1]);
        coords.push_back(dfobs_shell2bf[s2]);
        return coords;
    }
    auto bdata() {
        return jlcxx::make_julia_array(this->_bdata,this->bsz);
    }
    auto jdata() {
        return jlcxx::make_julia_array(this->_jdata,this->jsz);
    }
};




JLCXX_MODULE Libint2(jlcxx::Module& mod)
{
    mod.method("libint2_init",&libint2_init);
    mod.method("libint2_finalize",&libint2_finalize);
    mod.add_type<Atom>("Atom")
        .constructor<int,double,double,double>()
        .method("Z",&Atom::Z)
        .method("get_pos",&Atom::get_pos)
        .method("set_pos",&Atom::set_pos);

    mod.add_type<Molecule>("Molecule")
        .constructor<std::string&>()
        .constructor<jlcxx::ArrayRef<long>,jlcxx::ArrayRef<double>>()
        .method("get_Atom",&Molecule::get_Atom)
        .method("get_Atoms",&Molecule::get_Atoms)
        .method("get_size",&Molecule::get_size)
        .method("push_back",&Molecule::push_back);

    mod.add_type<BasisSet>("BasisSet")
        .constructor<std::string&, std::string&>()
        .constructor<std::string&, Molecule>()
        .method("nao",&BasisSet::nao)
        .method("getsize",&BasisSet::getsize)
        .method("print_out",&BasisSet::print_out)
        .method("max_nprim",&BasisSet::max_nprim)
        .method("set_pure",&BasisSet::set_pure)
        .method("max_l",&BasisSet::max_l);
        
    mod.add_type<OverlapEngine>("OverlapEngine")
        .constructor<int, int>()
        .method("compute",&OverlapEngine::compute)
        .method("chunk",&OverlapEngine::chunk)
        .method("data",&OverlapEngine::data)
        .method("startpoint",&OverlapEngine::startpoint);

    mod.add_type<DipoleEngine>("DipoleEngine")
        .constructor<int, int, double, double, double>()
        .method("compute",&DipoleEngine::compute)
        .method("chunk",&DipoleEngine::chunk)
        .method("mux",&DipoleEngine::mux)
        .method("muy",&DipoleEngine::muy)
        .method("muz",&DipoleEngine::muz)
        .method("startpoint",&DipoleEngine::startpoint);

    mod.add_type<NuclearEngine>("NuclearEngine")
        .constructor<int,int,Molecule>()
        .method("compute",&NuclearEngine::compute)
        .method("startpoint",&NuclearEngine::startpoint)
        .method("data",&NuclearEngine::data)
        .method("chunk",&NuclearEngine::chunk);

    mod.add_type<KineticEngine>("KineticEngine")
        .constructor<int,int>()
        .method("compute",&KineticEngine::compute)
        .method("startpoint",&KineticEngine::startpoint)
        .method("data",&KineticEngine::data)
        .method("chunk",&KineticEngine::chunk);

    mod.add_type<ERIEngine>("ERIEngine")
        .constructor<int,int>()
        .method("compute",&ERIEngine::compute)
        .method("startpoint",&ERIEngine::startpoint)
        .method("data",&ERIEngine::data)
        .method("chunk",&ERIEngine::chunk);

    mod.add_type<DFEngine>("DFEngine")
        .constructor<int,int>()
        .method("compute_b",&DFEngine::compute_b)
        .method("compute_j",&DFEngine::compute_j)
        .method("bchunk",&DFEngine::bchunk)
        .method("jchunk",&DFEngine::jchunk)
        .method("bdata",&DFEngine::bdata)
        .method("jdata",&DFEngine::jdata)
        .method("bstartpoint",&DFEngine::bstartpoint)
        .method("jstartpoint",&DFEngine::jstartpoint);
}
