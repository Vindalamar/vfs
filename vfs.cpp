// Требуется С++20

#include <filesystem>
#include <iostream>
#include <mutex>
#include <fstream>
#include <thread>
#include<vector> 
#include <algorithm>
#include <unordered_map>

namespace fs = std::filesystem;

namespace TestTask
{
	enum class fileState
	{
		Read, Write, Undef
	};

	struct File
	{
		std::fstream fileStream;
		std::string filename;

		fileState fState = fileState::Undef;
		size_t* pointer;
		size_t index;

		std::string writeBuff;

		File(std::string const name , fileState mode , size_t* p = nullptr,  size_t i = 0)
			{
				filename = name;
				pointer = p;
				fState = mode;
				index = i;
			}

			~File()
			{

			}
	};

	// Вы имеете право как угодно задать содержимое этой структуры

	struct IVFS
	{
		virtual File* Open(const char *name) = 0;	// Открыть файл в readonly режиме. Если нет такого файла или же он открыт во writeonly режиме - вернуть nullptr
		virtual File* Create(const char *name) = 0;	// Открыть или создать файл в writeonly режиме. Если нужно, то создать все нужные поддиректории, упомянутые в пути. Вернуть nullptr, если этот файл уже открыт в readonly режиме.
		virtual size_t Read(File *f, char *buff, size_t len) = 0;	// Прочитать данные из файла. Возвращаемое значение - сколько реально байт удалось прочитать
		virtual size_t Write(File *f, char *buff, size_t len) = 0;	// Записать данные в файл. Возвращаемое значение - сколько реально байт удалось записать
		virtual void Close(File *f) = 0;	// Закрыть файл	
	};

	

	struct VFS: public IVFS
	{
		typedef std::pair<std::string, size_t> fileStart;

		std::fstream fileS;

		std::mutex m1, m2;
		public:

		std::vector<fileStart> nameList;
		std::unordered_map <std::string, File*> openedFiles;

		VFS() {
			if(fs::exists(fs::path("filenames.data"))) {
				std::string name;
				std::string delimiter = " | ";
				size_t pos = 0;
				fileS.open("filenames.data", std::fstream::in);

				if (!fileS.is_open())
					return;

				while(std::getline(fileS, name)) {
					pos = name.find(delimiter);
					nameList.push_back(std::pair(name.substr(0, pos), std::stoi(name.substr(pos + delimiter.length()))));
				}

				fileS.close();
    		}

			if(!fs::exists(fs::path("mainFile.data")))
				std::ofstream("mainFile.data");
		}

		~VFS() {
			fileS.open("filenames.data", std::fstream::out);
			for (fileStart p : nameList)
				fileS << p.first << " | " << p.second << "\n";
		}

		File* Open(const char *name)
		{
			std::lock_guard<std::mutex > lk(m1);

			if (openedFiles.find(name) != openedFiles.end())
				if(openedFiles[name]->fState == fileState::Write)
					return nullptr; 
				else
					return openedFiles[name];
			

			auto it = std::ranges::find(nameList, name, &std::pair<std::string, size_t>::first);

			if (it == nameList.end())
				return nullptr;


			File* f = new File(name , fileState::Read, &(*it).second, it - nameList.begin());

			openedFiles[name] = f;

			return f;
		}

		File* Create(const char *name)
		{	
			std::lock_guard<std::mutex > lk(m1);

			if (openedFiles.find(name) != openedFiles.end())
				if(openedFiles[name]->fState == fileState::Read)
					return nullptr; 
				else {
					openedFiles[name]->writeBuff.clear();
					return openedFiles[name];
				}
					

			auto it = std::ranges::find(nameList, name, &std::pair<std::string, size_t>::first);
			
			if(it != nameList.end()) {
				std::ifstream oldF("mainFile.data");
				std::ofstream newF("mainFileTemp.data");
				
				size_t before = (*it).second;

				char* buff = new char[before];

				oldF.read(buff, before);
				
				newF.write(buff, before);

				oldF.seekg(0, std::ios::end);
				size_t length = oldF.tellg(); 

				it = nameList.erase(it);

				if (it != nameList.end()) {
					
					size_t after = (*it).second;
					char* buff1 = new char[length - after];
					oldF.seekg(after);
					oldF.read(buff1, length - after);
					newF.write(buff1, length - after);
					delete[] buff1;

					while (it != nameList.end()) {
						(*it).second = (*it).second - after + before;
						++it;
					}
				}

				oldF.close();
				newF.close();
				fs::remove("mainFile.data");
				fs::rename("mainFileTemp.data", "mainFile.data");

				delete[] buff;
			}

			File* f = new File(name , fileState::Write);

			openedFiles[name] = f;
			
			return f;
		}


		size_t Read(File *f, char *buff, size_t len)
		{
			if (f->fState != fileState::Read)
				return 0;

			f->fileStream.open("mainFile.data", std::fstream::in);

			f->fileStream.seekg(nameList[f->index].second);

			f->fileStream.read(buff, len);

			f->fileStream.close();

			return f->fileStream.gcount();
		}


		size_t Write(File *f, char *buff, size_t len)
		{
			if (f->fState != fileState::Write)
				return 0;

			std::lock_guard<std::mutex > lk(m2);

			size_t before = f->writeBuff.size(); 

			f->writeBuff += buff;
			
			return  f->writeBuff.size() - before;
		}

		void Close(File *f)
		{	
			std::lock_guard<std::mutex > lk(m2);

			if (f->fState == fileState::Write) {
				nameList.push_back(std::pair(f->filename, std::fstream("mainFile.data", std::fstream::in | std::fstream::ate).tellg()));
				f->fileStream.open("mainFile.data", std::fstream::app);
				if (!f->fileStream.is_open())
					return;
				f->fileStream.write(f->writeBuff.c_str(), f->writeBuff.size());
			}
			
			openedFiles.erase(f->filename);
			delete f;
		}
	};
}

	int main() /* demo */ {
		
		
		TestTask::VFS vfs;

		TestTask::File* f1 = vfs.Create("new1.txt");
		TestTask::File* f2 = vfs.Create("new2.txt");
		
		std::string s1 = "Давно выяснено, что при оценке дизайна и композиции читаемый текст мешает сосредоточиться. Lorem Ipsum";
		std::string s2 = "Жил был хатабыч";


		size_t i1 = vfs.Write(f1, s1.data(), s1.size());
		size_t i2 = vfs.Write(f2, s2.data(), s2.size());


		vfs.Close(f1);
		vfs.Close(f2);

		// char buff1[100];

		// char buff2[100];

		char* buff = new char[i1];
		char* buff2 = new char[i2];

		f1 = vfs.Open("new1.txt");
		f2 = vfs.Open("new2.txt");

		size_t r1 = vfs.Read(f1, buff, i1 + 1);
		size_t r2 = vfs.Read(f2, buff2, i2);


		std::cout << buff;
		std::cout << buff2;

		vfs.Close(f1);
		vfs.Close(f2);
	
		delete[] buff;
		delete[] buff2;

	}