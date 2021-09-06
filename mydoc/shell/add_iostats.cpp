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
#define SUBXLATOR_BEGIN "subvolumes "

using namespace std;

class Xlator;
class Xlator{
private:
    string volname;
    Xlator* next;
    Xlator* prev;
    vector<Xlator*> child;
    int pos;
    string xlator_config;
    vector<string> iostat_config;
    static int count;
public:
    Xlator();
    // 在当前xlator之后增加层iostat xlator
    void addiostats(Xlator* iostat);
    void init(string xlator_config, map<string, string>& mpXlator);

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
    bool buildNewConfigFile();
    Xlator* find_first_iostat();
};



string getvolname(string xlator_config) {
    int volnamepos = xlator_config.find(XLATOR_BEGIN);
    string volname;
    for (int i = volnamepos + strlen(XLATOR_BEGIN); !isspace(xlator_config[i]); i++) {
        volname = volname + xlator_config[i];
    }
    return volname;
}


vector<string> getsubvolname(string xlator_config) {
    int volnamepos = xlator_config.find(SUBXLATOR_BEGIN);
    string volname;
    for (int i = volnamepos + strlen(SUBXLATOR_BEGIN); (xlator_config[i] != '\n'); i++) {
        volname = volname + xlator_config[i];
    }
    vector<string> volnames; 
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
    return volnames;
}




int Xlator::count = 0;

void
Xlator::addiostats(Xlator* iostat) {
    static int num = 0;
    if (this->volname == "debug/io-stats" || this->next == nullptr
        || this->next->volname == "debug/io-stats") {
        return;
    }
    vector<Xlator*> child_temp = child;
    for (auto beg = child_temp.begin(); beg != child_temp.end(); beg++) {
        Xlator* curchild = *beg;

        string curchild_iostatvolname = iostat->volname + to_string(num++);


        string add_config = iostat->xlator_config;
        // 替换新的iostat name
        int pos = add_config.find(iostat->volname);
        add_config.replace(pos, iostat->volname.length(), curchild_iostatvolname);

        // 修改新的iostat subvolname
        pos = add_config.rfind(iostat->next->volname);
        add_config.replace(pos, iostat->next->volname.length(), curchild->volname);

        // 修改当前xlator的subvol
        pos = xlator_config.rfind(curchild->volname);
        xlator_config.replace(pos, xlator_config.length(), add_config);

        iostat_config.push_back(add_config);
    }
}







void 
Xlator::init(string parseVolname, map<string, string>& mpXlator) {
    this->xlator_config = mpXlator[parseVolname];
    volname = parseVolname;
    vector<string> subvolnums = getsubvolname(xlator_config);
    Xlator* cur = this;
    this->pos = Xlator::count++;
    for (int i = 0; i < subvolnums.size(); i++) {
        Xlator* subvolnum = new Xlator();

        subvolnum->prev = cur;
        cur->next = subvolnum;
        child.push_back(subvolnum);

        subvolnum->init(getvolname(subvolnums[i]), mpXlator);
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

    string strbuf(buf);
    vector<string> xlatorlist;

    
    int pos = strbuf.find(XLATOR_BEGIN);
    int endpos = 0;
    while (pos != string::npos) {

        endpos = strbuf.find(XLATOR_BEGIN, pos+1);

        if (endpos == string::npos) {
            string endXlator(strbuf, pos, string::npos);
            xlatorlist.push_back(endXlator);
            break;
        }

        string newXlator(strbuf, pos, endpos-pos);
        xlatorlist.push_back(newXlator);
        
        pos = endpos;
    }

    // int xlator_count = 0;
    // for (auto b = xlatorlist.begin(); b != xlatorlist.end(); b++) {
    //     cout << "the "<<xlator_count++ << " xlator configure file:" << endl;
    //     cout << *b << endl;
    // }
    return buildGraph(xlatorlist);

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


bool 
Graph::buildNewConfigFile() {

    return false;
}



bool 
Graph::setpath(string path) {
    this->path = path;

    return true;
}


Xlator* 
Graph::find_first_iostat() {
    return nullptr;
}




int main() {

    Graph* mygraph = new Graph();
    mygraph->setpath("/home/DEEPROUTE/xiaobaowen/work/shell/trusted-vol.tcp-fuse.vol");

    mygraph->initGraph();

    
    return 0;
}

