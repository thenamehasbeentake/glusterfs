#include <vector>
#include <string>
#include <map>
#include <iostream>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

#define XLATOR_BEGIN "volume "
#define XLATOR_END "end-volume"
#define SUBXLATOR_BEGIN "subvolumes "
#define XLATOR_TYPE_BEGIN "type "
#define IOSTATS_XLATOR_TYPE "debug/io-stats"
#define XLATOR_TYPE_BEGIN "type "
#define XLATOR_PATH "/var/log/glusterfs/xlator.list"
using namespace std;

class Xlator;
class Xlator{
private:
    string volname;
    string type;
    Xlator* next;
    Xlator* prev;
    vector<Xlator*> child;
    int pos;
    string xlator_config;
    vector<string> iostat_config;
    static int count;
public:
    Xlator() {
        next = nullptr;
        prev = nullptr;
        child.clear();
        iostat_config.clear();
    };
    // 在当前xlator之后增加层iostat xlator
    void addiostats(Xlator* iostat);
    void addiostats_recursion(Xlator* iostat);
    void init(string xlator_config, map<string, string>& mpXlator);
    Xlator* find_first_iostat();
    vector<string> getNewConfig();
    void display();
    Xlator* getNext() { return next; }
    string getVolname() { return volname; }
    string getType() { return type; }
};


class Graph{
private:
    Xlator* head;
    string path;
    map<string, string> makemp(vector<string>& xlatorlist);

public:
    Graph():head(nullptr), path(){
        
    }
    
    Graph(Xlator* head, string path):head(head), path(path) {
        
    }
    bool setpath(string path);
    bool initGraph();
    bool buildGraph(vector<string>& xlatorlist);
    string buildNewConfigFile();
    size_t write_back(string s);
    string getXlatorNameList();
    void write_xlaorlist(string str);
    ~ Graph();
};



string getvolname(string xlator_config) {
    int volnamepos = xlator_config.find(XLATOR_BEGIN);
    string volname;
    for (int i = volnamepos + strlen(XLATOR_BEGIN); !isspace(xlator_config[i]); i++) {
        volname = volname + xlator_config[i];
    }
    return volname;
}

string gettype(string xlator_config) {
    int volnamepos = xlator_config.find(XLATOR_TYPE_BEGIN);
    string volname;
    for (int i = volnamepos + strlen(XLATOR_TYPE_BEGIN); !isspace(xlator_config[i]); i++) {
        volname = volname + xlator_config[i];
    }
    return volname;
}

vector<string> getsubvolname(string xlator_config) {
    int volnamepos = xlator_config.find(SUBXLATOR_BEGIN);

    string volname;
    vector<string> volnames; 


    if (volnamepos == string::npos) {
        return volnames;
    }
    for (int i = volnamepos + strlen(SUBXLATOR_BEGIN); xlator_config[i] != '\n'; i++) {
        volname = volname + xlator_config[i];
    }

    // cout << volname << endl;


    string subvolname;
    for (int i = 0; i < volname.size(); i++) {
        if (volname[i] == ' ') {
            volnames.push_back(subvolname);
            subvolname = "";
            while(i+1 < volname.size() && volname[i+1] == ' ') {
                i++;
            }
            continue;
        }
        subvolname = subvolname + volname[i];
    }
    volnames.push_back(subvolname);

    // cout << subvolname << endl;
    return volnames;
}




int Xlator::count = 0;



void
Xlator::addiostats(Xlator* iostat) {
    static int num = 0;
    if (this->type == IOSTATS_XLATOR_TYPE || this->next == nullptr
        || this->next->type == IOSTATS_XLATOR_TYPE) {
        return;
    }
    vector<Xlator*> child_temp = child;
    int temp = child_temp.size() + num++;
    for (auto beg = child_temp.begin(); beg != child_temp.end(); beg++) {
        Xlator* curchild = *beg;

        string curchild_iostatvolname = iostat->volname + "-" + to_string(--temp);


        string add_config = iostat->xlator_config;
        // 替换新的iostat name
        int pos2 = add_config.find(XLATOR_BEGIN);
        // cout << pos2 << endl;
        int pos = add_config.find(iostat->volname, pos2+sizeof(XLATOR_BEGIN)-1);

        // cout << pos << endl;
        add_config.replace(pos, iostat->volname.length(), curchild_iostatvolname);

        // 修改新的iostat subvolname
        pos = add_config.rfind(iostat->next->volname);
        add_config.replace(pos, iostat->next->volname.length(), curchild->volname);

        // 修改当前xlator的subvol
        pos = xlator_config.rfind(curchild->volname);
        xlator_config.replace(pos, curchild->volname.length(), curchild_iostatvolname);

        iostat_config.push_back(add_config);
    }
}



Xlator* 
Xlator::find_first_iostat() {
    Xlator* cur = this;
    // 简单点
    while (cur) {
        if (cur->type == IOSTATS_XLATOR_TYPE) {
            return cur;
        }
        cur = cur->next;
    }
    return nullptr;
}


void 
Xlator::addiostats_recursion(Xlator* iostat) {
    Xlator* cur = this;
    
    while (cur) {
        cur->addiostats(iostat);
        cur = cur->next;
    }
}





void 
Xlator::init(string parseVolname, map<string, string>& mpXlator) {
    if (parseVolname.empty()) {
        return;
    }

    xlator_config = mpXlator[parseVolname];
    volname = parseVolname;
    type = gettype(xlator_config);

    vector<string> subvolnums = getsubvolname(xlator_config);
    Xlator* cur = this;
    this->pos = Xlator::count++;

    for (int i = subvolnums.size() -1; i >= 0; i--) {

        Xlator* subvolnum = new Xlator();

        subvolnum->prev = cur;
        cur->next = subvolnum;
        child.push_back(subvolnum);
        subvolnum->init(subvolnums[i], mpXlator);

        cur = subvolnum;
    }

}


vector<string> 
Xlator::getNewConfig() {
    Xlator* cur = this;
    vector<string> conf_list;

    while (cur) {
        conf_list.push_back(cur->xlator_config);

        // if (!cur->iostat_config.empty())
        //     conf_list.push_back(cur->iostat_config.back());
        for (auto itr = cur->iostat_config.rbegin(); itr != cur->iostat_config.rend(); itr++) {
            conf_list.push_back(*itr);
        }
        // debug
        cur = cur->next;
    }
    return conf_list;
}


void 
Xlator::display() {

    Xlator* cur = this;

    while (cur) {
        cout << cur->xlator_config << endl;
        cur = cur->next;
    }
}





bool 
Graph::initGraph() {
    const int bufsize = 131072;
    char buf[bufsize];
    int _size;

    int fd = open(path.c_str(), O_RDWR);
    _size = read(fd, buf, bufsize);

    // printf("%s\n", buf);
    assert(_size <= bufsize);

    int fd2 = open((path + "." + to_string(time(NULL))).c_str(), O_RDWR | O_CREAT);
    write(fd2, buf, _size);

    close(fd);
    close(fd2);

    string strbuf(buf);
    vector<string> xlatorlist;

    
    int pos = strbuf.find(XLATOR_BEGIN);
    int endpos = 0;
    while (pos != string::npos) {

        endpos = strbuf.find(XLATOR_END, pos+1);

        if (endpos == string::npos) {
            // string endXlator(strbuf, pos, string::npos);
            // xlatorlist.push_back(endXlator);
            break;
        }

        string newXlator(strbuf, pos, endpos-pos + sizeof(XLATOR_END));
        xlatorlist.push_back(newXlator);
        
        pos = endpos+sizeof(XLATOR_END);
    }

    // int xlator_count = 0;
    // for (auto b = xlatorlist.begin(); b != xlatorlist.end(); b++) {
    //     cout << "the "<<xlator_count++ << " xlator configure file:" << endl;
    //     cout << *b << endl;
    // }
    buildGraph(xlatorlist);

    // head->display();
    return true;

}


map<string, string> 
Graph::makemp(vector<string>& xlatorlist) {
    map<string, string> mp;
    for (int i = 0; i < xlatorlist.size(); i++) {
        string xlator_config = xlatorlist[i];
        string volname = getvolname(xlator_config);
        mp[volname] = xlator_config;
    }
    return mp;
}



bool 
Graph::buildGraph(vector<string>& xlatorlist) {
    if (xlatorlist.empty()) {
        return false;
    }
    head = new Xlator();
    map<string, string> mpXlator = makemp(xlatorlist);

    head->init(getvolname(xlatorlist.back()), mpXlator);
    return true;
}


string 
Graph::buildNewConfigFile() {
    if (nullptr == head) {
        return "";
    }

    Xlator* iostats = head->find_first_iostat();

    assert(nullptr != iostats);

    head->addiostats_recursion(iostats);
    vector<string> conf_list = head->getNewConfig();


    string conf = "";
    for(auto rbegin = conf_list.rbegin(); rbegin != conf_list.rend(); rbegin++) {
        conf = conf + *rbegin;
    }

    return conf;
}



bool 
Graph::setpath(string path) {
    this->path = path;

    return true;
}


size_t 
Graph::write_back(string s) {
    int fd = open(path.c_str(), O_RDWR | O_TRUNC);

    write(fd, s.c_str(), s.size());

    close(fd);
}

string 
Graph::getXlatorNameList() {
    string xlatornamelist;

    Xlator* cur = head;

    while (cur) {
        if (cur->getType() != IOSTATS_XLATOR_TYPE)
            xlatornamelist = xlatornamelist + cur->getVolname() + "\n";
        cur = cur->getNext();
    }
    return xlatornamelist;
}

void 
Graph::write_xlaorlist(string str) {

    int fd = open(XLATOR_PATH, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        cout << "error open fail" << endl;
    }

    write(fd, str.c_str(), str.size());

    fdatasync(fd);

    close(fd);
}

Graph::~ Graph() {
    vector<Xlator*> list;

    Xlator* cur = head;

    while (cur) {
        list.push_back(cur);
        cur = cur->getNext();
    }
    for (auto & itr : list) {
        delete itr;
    }
}

int main(int argc, char *argv[]) {

    Graph* mygraph = new Graph();
    // mygraph->setpath("/home/DEEPROUTE/xiaobaowen/work/shell/trusted-vol.tcp-fuse.vol");
    // mygraph->setpath("/home/DEEPROUTE/xiaobaowen/work/code/glusterfs/mydoc/shell/trusted-vol2.tcp-fuse.vol");

    assert(argc == 2);

    if (0 != access(argv[1], F_OK | R_OK | W_OK)) {
        cout << "can't access, errno = "<< errno << endl;
        return -1;
    }

    mygraph->setpath(argv[1]);
    mygraph->initGraph();
    
    string profileXlator = mygraph->getXlatorNameList();

    cout << profileXlator << endl;
    mygraph->write_xlaorlist(profileXlator);

    string newfile = mygraph->buildNewConfigFile();
    mygraph->write_back(newfile);

    
    delete mygraph;

    return 0;
}

