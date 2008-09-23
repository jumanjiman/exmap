/*
 * (c) John Berthels 2005 <jjberthels@gmail.com>. See COPYING for license.
 */
#include <Trun.hpp>
#include "Exmap.hpp"
#include "Pcre.hpp"

#include <sstream>
#include <list>
#include <map>

class ExmapTest : public Test
{
public:
    bool setup();
    bool run();
    bool teardown();
private:
    unsigned long get_pid_size_from_ps(pid_t pid);
    
    static const std::string UTILDIR;
    static const std::string SA_EXE;
    static const std::string MI_EXE;
    static const std::string MI_DAT;
    static const std::string SA_LIB;
    static const int NUM_INSTANCES;
    static const int ARRAY_SIZE;
    static const int NUM_ARRAYS = 10;
    std::list<FILE *> _popen_handles;

    struct array_info
    {
	std::string name;
	int resident_percent;
	int writable_percent;
	bool shared;
	bool initialised;
    };
    int num_initialised_arrays();
    static const struct array_info ARRAY_INFO[];
};


using namespace std;
using namespace Exmap;
using namespace jutil;
using namespace Pcre;

const string ExmapTest::UTILDIR = "../tools";
const string ExmapTest::SA_EXE = UTILDIR + "/sharedarray";
const string ExmapTest::MI_EXE = UTILDIR + "/mapit";
const string ExmapTest::MI_DAT = "mapit.dat";
const string ExmapTest::SA_LIB = UTILDIR + "/libsharedarray.so";
const int ExmapTest::NUM_INSTANCES = 10;
const int ExmapTest::ARRAY_SIZE = 100 * Elf::PAGE_SIZE;
const struct ExmapTest::array_info ExmapTest::ARRAY_INFO[] = {

    { "donttouch", 0, 0, false, true },
    { "readme", 100, 0, true, true },
    { "writeme", 100, 100, false, true },
    { "readhalf", 50, 0, true, true },
    { "writehalf", 50, 50, false, true },

    { "uninit_donttouch", 0, 0, false, false },
    { "uninit_readme", 100, 0, true, false },
    { "uninit_writeme", 100, 100, false, false },
    { "uninit_readhalf", 50, 0, true, false },
    { "uninit_writehalf", 50, 50, false, false },
};

int ExmapTest::num_initialised_arrays()
{
    int result = 0;
    for (int i = 0; i < NUM_ARRAYS; ++i) {
	if (ARRAY_INFO[i].initialised) {
	    result++;
	}
    }
    return result;
}

bool ExmapTest::run()
{
    SysInfoPtr sysinfo(new LinuxSysInfo);
    Snapshot snap(sysinfo);

    is(snap.num_procs(), 0, "zero procs before load");
    ok(snap.load(), "can load snapshot");
    ok(snap.num_procs() > 0, "some procs after load");

    list<ProcessPtr> allprocs = snap.procs();
    ok(!allprocs.empty(), "can get a list of procs");
    list<ProcessPtr>::iterator proc_it;
    list<ProcessPtr> procs;
    for (proc_it = allprocs.begin(); proc_it != allprocs.end(); ++proc_it) {
	string cmdline = (*proc_it)->cmdline();
	if (cmdline== SA_EXE) {
	    procs.push_back(*proc_it);
	}
    }
    is((int) procs.size(), NUM_INSTANCES, "can find all our sharedarray procs");

    ProcessPtr proc = procs.front();
    SizesPtr sizes = proc->sizes();
    ok(sizes->val(Sizes::VM) > NUM_ARRAYS * ARRAY_SIZE, "image is big enough");

    unsigned long ps_size = get_pid_size_from_ps(proc->pid());
    is(sizes->val(Sizes::VM), ps_size, "exmap info matches ps output");

    ok(sizes->val(Sizes::RESIDENT) > 0, "nonzero resident size");
    ok(sizes->val(Sizes::EFFECTIVE_RESIDENT) > 0, "nonzero eff resident size");
    ok(sizes->val(Sizes::EFFECTIVE_RESIDENT) < sizes->val(Sizes::RESIDENT),
       "effective is smaller than eff resident");

    ok(sizes->val(Sizes::MAPPED) > 0, "nonzero mapped size");
    ok(sizes->val(Sizes::EFFECTIVE_MAPPED) > 0, "nonzero eff mapped size");
    ok(sizes->val(Sizes::EFFECTIVE_MAPPED) < sizes->val(Sizes::MAPPED),
       "effective is smaller than eff mapped");

    
    list<FilePtr> files;
    list<FilePtr>::iterator file_it;
    list<FilePtr> all_files = snap.files();
    
    ok(all_files.size() > 0, "can find some files");

    Regexp re;
    re.compile(SA_LIB + "$");
    for (file_it = all_files.begin(); file_it != all_files.end(); ++file_it) {
	string name = (*file_it)->name();
	if (re.matches(name)) {
	    files.push_back(*file_it);
	}
    }
    is((int) files.size(), 1, "file only listed once");
    FilePtr file = files.front();
    ok(file->is_elf(), "file recognised as elf file");

    list<ProcessPtr> procs_per_file = file->procs();
    is((int) procs_per_file.size(), NUM_INSTANCES,
       "right number of procs mapping the file");
    for (proc_it = procs_per_file.begin();
	 proc_it != procs_per_file.end();
	 ++proc_it) {
	ok(proc->cmdline() == SA_EXE, "each proc has correct cmdline");
    }


    for (proc_it = procs.begin();
	 proc_it != procs.end();
	 ++proc_it) {
	sizes = proc->sizes(file);
	long arrays_size = NUM_ARRAYS * ARRAY_SIZE;
	float delta = std::abs(arrays_size - (long) sizes->val(Sizes::VM));
	delta /= arrays_size;
	ok(delta < 0.01, "Shared lib has correct size in each proc");
    }
    
    Elf::SectionPtr text = file->elf()->section(".text");
    ok(text, "can find text section");
    ok(text->size() > 0, "text section has nonzero size");
    sizes = procs.front()->sizes(file, text->mem_range());
    is(sizes->val(Sizes::RESIDENT), (unsigned long) 2 * text->size(),
       "lib text is resident and mapped twice (!)");
    
    Elf::SectionPtr bss = file->elf()->section(".bss");
    ok(bss, "can find bss section");
    ok(bss->size() > 0, "bss section has nonzero size");
    SizesPtr bss_sizes = procs.front()->sizes(file, bss->mem_range());
    ok(bss_sizes, "can get sizes for bss section");

    Elf::SectionPtr data = file->elf()->section(".data");
    ok(data, "can find data section");
    ok(data->size() > 0, "data section has nonzero size");
    SizesPtr data_sizes = procs.front()->sizes(file, data->mem_range());
    ok(data_sizes, "can get sizes for data section");

    is(data->size(), bss->size(), "data and bss sections have same size");

    is_approx(data_sizes->val(Sizes::MAPPED),
	      bss_sizes->val(Sizes::MAPPED),
	      Elf::PAGE_SIZE,
	      "data and bss mapped within page of each other");
    
    is_approx(data_sizes->val(Sizes::RESIDENT),
	      bss_sizes->val(Sizes::RESIDENT),
	      Elf::PAGE_SIZE,
	      "data and bss resident within page of each other");


    unsigned long bss_resident_arrays_size = 0;
    unsigned long data_resident_arrays_size = 0;
    unsigned long bss_writable_arrays_size = 0;
    unsigned long data_writable_arrays_size = 0;
    for (int i = 0; i < NUM_ARRAYS; ++i) {
	if (ARRAY_INFO[i].initialised) {
	    data_resident_arrays_size
		+= ARRAY_SIZE * ARRAY_INFO[i].resident_percent / 100;
	    data_writable_arrays_size
		+= ARRAY_SIZE * ARRAY_INFO[i].writable_percent / 100;
	}
	else {
	    bss_resident_arrays_size
		+= ARRAY_SIZE * ARRAY_INFO[i].resident_percent / 100;
	    bss_writable_arrays_size
		+= ARRAY_SIZE * ARRAY_INFO[i].writable_percent / 100;
	}
    }

    for (proc_it = procs.begin();
	 proc_it != procs.end();
	 ++proc_it) {
	
	sizes = proc->sizes(file, data->mem_range());
	is_approx(sizes->val(Sizes::RESIDENT),
		  data_resident_arrays_size,
		  Elf::PAGE_SIZE,
		  "resident size for data in proc correct with a page");
	is_approx(sizes->val(Sizes::WRITABLE),
		  data_writable_arrays_size,
		  Elf::PAGE_SIZE,
		  "writable size for data in proc correct with a page");
	
	sizes = proc->sizes(file, bss->mem_range());
	is(sizes->val(Sizes::RESIDENT), bss_resident_arrays_size,
		      "correct resident size for bss in proc");
	is(sizes->val(Sizes::WRITABLE), bss_writable_arrays_size,
		      "correct writable size for bss in proc");
    }


    for (int i = 0; i < NUM_ARRAYS; ++i) {
	const struct array_info *info = ARRAY_INFO + i;
	string symname = info->name;
	Elf::SymbolPtr sym = file->elf()->symbol(symname);
	ok(sym, "can find symbol " + symname);
	is(sym->size(), ARRAY_SIZE, symname + " has correct size");
	
	sizes = proc->sizes(file, sym->range());
	is((int) sizes->val(Sizes::RESIDENT),
	   ARRAY_SIZE * info->resident_percent / 100,
	   symname + " has correct resident size");

	is((int) sizes->val(Sizes::WRITABLE),
	   ARRAY_SIZE * info->writable_percent / 100,
	   symname + " has correct writable size");

	// Uninitialised space which is only read appears to be shared
	// amongst every proc in the system (a 'zero page'?). This is
	// good from a low-memusage point of view, but it means that
	// it is shared among nearly all running procs to varying
	// degrees. So we can't really account for it.
	if (symname == "uninit_readme" || symname == "uninit_readhalf") {
	    continue;
	}

	int expected_eff_resident = ARRAY_SIZE * info->resident_percent / 100;
	if (info->shared) {
	    expected_eff_resident /= NUM_INSTANCES;
	}
	// approximate match since the percentage (fixed pt)
	// arithmetic could put us off by a factor of 1/100 (and does...:-)
	is_approx((int) sizes->val(Sizes::EFFECTIVE_RESIDENT),
		  expected_eff_resident,
		  1 + (expected_eff_resident / 100),
		  symname + " has correct effective size");
    }


    // Non-elf maps

    procs.clear();
    for (proc_it = allprocs.begin(); proc_it != allprocs.end(); ++proc_it) {
	string cmdline = (*proc_it)->cmdline();
	if (cmdline == MI_EXE) {
	    procs.push_back(*proc_it);
	}
    }
    is((int) procs.size(), NUM_INSTANCES, "can find all our mapit procs");
    proc = procs.front();

    files.clear();
    re.compile("/" + MI_DAT + "$");
    for (file_it = all_files.begin(); file_it != all_files.end(); ++file_it) {
	string name = (*file_it)->name();
	if (re.matches(name)) {
	    files.push_back(*file_it);
	}
    }
    is((int) files.size(), 1, MI_DAT + " file only listed once");
    file = files.front();

    ok(!file->is_elf(), MI_DAT + " isn't an elf file");
    off_t fsize = 0;
    unsigned long mi_data_size = 0;
    ok(file_size(MI_DAT, fsize), "can get file size");
    mi_data_size = fsize;
    ok(mi_data_size > 0, "file has nonzero size");
    
    for (proc_it = procs.begin();
	 proc_it != procs.end();
	 ++proc_it) {
	sizes = (*proc_it)->sizes(file);
	is(sizes->val(Sizes::VM), mi_data_size, "correct data file size");
	is(sizes->val(Sizes::RESIDENT), mi_data_size,
	   "whole data file is resident");
	is(sizes->val(Sizes::EFFECTIVE_RESIDENT),
	   mi_data_size / NUM_INSTANCES,
	   "data file is shared between instances correctly");
    }
    
    return true;
}


bool ExmapTest::setup()
{
    plan(195);
    
    const string ld_path_env = "LD_LIBRARY_PATH";
    const char *cp = getenv(ld_path_env.c_str());
    string ld_path;
    if (cp != NULL) {
	ld_path = cp;
    }
    ld_path += ":" + UTILDIR;
    string putenv_arg = ld_path_env + "=" + ld_path;
    putenv(const_cast<char *>(putenv_arg.c_str()));

    for (int i = 0; i < NUM_INSTANCES; ++i) {
	FILE *fp = popen(SA_EXE.c_str(), "w");
	ok(fp, "can start instance of " + SA_EXE);
	_popen_handles.push_back(fp);
	fp = popen(MI_EXE.c_str(), "w");
	ok(fp, "can start instance of " + MI_EXE);
	_popen_handles.push_back(fp);
    }
    
    return true;
}

bool ExmapTest::teardown()
{
    while (!_popen_handles.empty()) {
	FILE *fp = _popen_handles.front();
	if (fp != NULL) {
	    fprintf(fp, "\n");
	    pclose(fp);
	}
	_popen_handles.pop_front();
    }
    return true;
}


unsigned long ExmapTest::get_pid_size_from_ps(pid_t pid)
{
    list<string> lines;
    list<string> captures;
    ok(read_proc_output("ps -e -o pid,vsz", lines), "can read proc output");
    Regexp re;
    stringstream sstr;
    sstr << "\\s*" << pid << "\\s";
    re.compile(sstr.str());
    re.grep(lines);
    is((int) lines.size(), 1, "found line for pid");
    re.compile("\\s*(\\d+)\\s+(\\d+)$");
    re.match_capture(lines.front(), captures);
    is((int) captures.size(), 2, "Found my captures");
    int size = atoi(captures.back().c_str());
    return size * 1024;
}

RUN_TEST_CLASS(ExmapTest);
