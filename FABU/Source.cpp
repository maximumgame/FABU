#include <iostream>
#include <fstream>
#include <string>
#include "SHA1.h"
#include <boost\property_tree\ptree.hpp>
#include <boost\property_tree\json_parser.hpp>
#include <boost\foreach.hpp>
#include <urlmon.h>
#include <ShlObj.h>
#include <vector>
#include <Objbase.h>
#include <WinInet.h>

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "Wininet.lib")

#import "BSPatchManaged.tlb" raw_interfaces_only

using namespace BSPatchManaged;
using namespace std;

typedef boost::property_tree::ptree::path_type path;

void createFolders(string);
BSTR ConvertMBSToBSTR(const string&);
void PatchFile(string file);

IBSPatch* pIBSPatch;

int main(int argc, char* argv[])
{
	string args = "";
	if (argc > 1)
	{
		for (int i = 0; i < argc; i++)
		{
			args += argv[i];
			args.append(" ");
		}
	}
	boost::property_tree::ptree pt;
	string file = "http://eldewrito.anvilonline.net/update.json";
	ifstream dew("dewrito.json");
	if (dew)
	{
		boost::property_tree::read_json("dewrito.json", pt);

		file = pt.get<string>("updateServiceUrl");
		
		cout << file << endl;
		dew.close();
	}

	createFolders("_dewbackup\\asdf"); //Conform to dewritoupdater so that it doesn't go berserk when backups aren't there
	//If original files in _dewbackup do not exist, dewritoupdater decides that it can't find dewrito.json nor can it connect to the default update server
	//so use same folder for backups
	
	URLDownloadToFile(NULL, _T(file.c_str()), _T("temp.json"), 0, NULL);
	file = "temp.json";

	boost::property_tree::read_json(file, pt);
	
	string version;
	BOOST_FOREACH(boost::property_tree::ptree::value_type &v, pt)
	{
		cout << v.first << endl; //top should always be newest (I thought we had older versions listed here aswell a long time ago, maybe I'm just crazy)
		version = v.first;
		continue;
	}

	string baseurl = pt.get<string>(path(version + "+baseUrl", '+'));
	cout << baseurl << endl;

	vector<string> patchFiles;

	BOOST_FOREACH(boost::property_tree::ptree::value_type &v, pt.get_child(path(version + "+patchFiles", '+')))
	{
		patchFiles.push_back(v.second.data());
	}
	

	BOOST_FOREACH(boost::property_tree::ptree::value_type &v, pt.get_child(path(version + "+files", '+')))
	{
		CSHA1 sha1;
		sha1.HashFile(v.first.c_str());
		sha1.Final();
		TCHAR szReport[50];
		sha1.ReportHash(szReport, CSHA1::REPORT_HEX_SHORT);
		cout << "Got   : " << v.first.data() << " : " << szReport << endl;
		cout << "Should: " << v.first.data() << " : " << v.second.data() << endl;
		
		createFolders(string(v.first.data()));

		bool isPatchFile = false;

		for (int i = 0; i < patchFiles.size(); i++)
		{
			if (string(v.first.data()).compare(patchFiles[i]) == 0 && string(v.second.data()).compare(szReport) != 0)
			{
				createFolders(("_dewbackup\\" + (string)v.first.data()));
				DeleteUrlCacheEntry((baseurl + (string)v.first.data() + ".bspatch").c_str());
				HRESULT hr = URLDownloadToFile(NULL, _T((baseurl + (string)v.first.data() + ".bspatch").c_str()), _T(((string)v.first.data() + ".bspatch").c_str()), 0, NULL);
				if (hr != S_OK)
				{
					cout << "URL FAIL: " << baseurl + v.first << endl;
				}
				isPatchFile = true;
			}
			else if (string(v.first.data()).compare(patchFiles[i]) == 0 && string(v.second.data()).compare(szReport) == 0)
			{
				patchFiles.erase(patchFiles.begin() + i);
			}
		}

		if (string(v.second.data()).compare(szReport) != 0 && !isPatchFile)
		{
			cout << "downloading...." << endl;
			DeleteUrlCacheEntry((baseurl + v.first.data()).c_str());
			HRESULT hr = URLDownloadToFile(NULL, _T((baseurl + v.first.data()).c_str()), _T(((string)v.first.data()).c_str()), 0, NULL);
			if (hr != S_OK)
			{
				cout << "URL FAIL: " << baseurl + v.first << endl;
			}
		}
		else
		{
			cout << "Good" << endl;
		}
	}
	DeleteFile("temp.json");

	//read the dewrito.json to see if current files that need to be patched are already here/backed up or just old version
	boost::property_tree::read_json("dewrito.json", pt);

	HRESULT hre = CoInitialize(NULL);
	pIBSPatch = NULL;
	// Took BinaryPatchUtility and made a dll for this specifically because its easier (the C# version is also about the same speed as being written in C, atleast I know this works)
	hre = CoCreateInstance(__uuidof(ManagedClass), NULL, CLSCTX_INPROC_SERVER, __uuidof(IBSPatch), (void**)&pIBSPatch);

	//refactored here so that we have dewrito.json to get original hashes from
	for (int i = 0; i < patchFiles.size(); i++)
	{
		CSHA1 sha1;
		sha1.HashFile(("_dewbackup\\" + patchFiles[i]).c_str());
		sha1.Final();
		TCHAR szReport[50];
		sha1.ReportHash(szReport, CSHA1::REPORT_HEX_SHORT);
		string origHash;
		BOOST_FOREACH(boost::property_tree::ptree::value_type &v, pt.get_child("gameFiles"))
		{
			if (((string)v.first.data()).compare(patchFiles[i]) == 0)
			{
				origHash = v.second.data();
				continue;
			}
		}
		cout << "Checking original in backup folder _dewbackup\\" << patchFiles[i] << endl;
		if (origHash.compare(szReport) == 0)
		{
			cout << "_dewbackup\\" << patchFiles[i] << " is original" << endl;
			PatchFile(patchFiles[i]);
		}
		else
		{
			cout << "Original hash did not match, checking base folder" << endl;
			sha1.Reset();
			sha1.HashFile(patchFiles[i].c_str());
			sha1.Final();
			sha1.ReportHash(szReport, CSHA1::REPORT_HEX_SHORT);
			if (origHash.compare(szReport) == 0)
			{
				MoveFile(patchFiles[i].c_str(), ("_dewbackup\\" + patchFiles[i]).c_str());
				cout << "Original " << patchFiles[i] << " matched hash, moved to backup" << endl;
				PatchFile(patchFiles[i]);
			}
			else
			{
				cout << "Failed to find original " << patchFiles[i] << endl;
			}
		}
	}
	CoUninitialize();
	if (args.find("-nolaunch") == string::npos)
		system("start eldorado.exe -launcher");
}

void createFolders(string str)
{
	if (str.find_last_of("\\") != string::npos)
	{
		size_t pos = str.find_last_of("\\");
		string path = "\\" + str.substr(0, pos);
		TCHAR cur[MAX_PATH];
		GetCurrentDirectory(MAX_PATH, cur);
		string fullPath = (cur + path);
		SHCreateDirectoryEx(NULL, fullPath.c_str(), NULL);
	}
}

BSTR ConvertMBSToBSTR(const string& str)
{
	int wslen = ::MultiByteToWideChar(CP_ACP, 0 /* no flags */,
		str.data(), str.length(),
		NULL, 0);

	BSTR wsdata = ::SysAllocStringLen(NULL, wslen);
	::MultiByteToWideChar(CP_ACP, 0 /* no flags */,
		str.data(), str.length(),
		wsdata, wslen);
	return wsdata;
}

void PatchFile(string file)
{
	BSTR input = ConvertMBSToBSTR("_dewbackup\\" + file);
	BSTR patch = ConvertMBSToBSTR(file + ".bspatch");
	BSTR output = ConvertMBSToBSTR(file);

	pIBSPatch->BsPatchFile(input, patch, output);
	cout << file << " patched" << endl;

	DeleteFile((file + ".bspatch").c_str());
}