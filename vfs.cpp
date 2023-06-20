// Требуется С++20

#include <filesystem>
#include <iostream>
#include <mutex>
#include <fstream>
#include <vector> 
#include <algorithm>
#include <unordered_map>
#include <cassert>
#include <shared_mutex>


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
		std::shared_timed_mutex mutex;
		public:

		// Все сохраненные файлы и их адреса
		std::vector<fileStart> nameList;
		// Открытые в данный момент файлы
		std::unordered_map <std::string, File*> openedFiles;

		VFS() {
			// При повтороном открытии системы в том же месте, все сохраненные файлы будут импортированы в новую систему
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
			// Сохранение всех файлов и их адресов
			fileS.open("filenames.data", std::fstream::out);
			for (fileStart p : nameList)
				fileS << p.first << " | " << p.second << "\n";
		}


		File* Open(const char *name) {
			std::lock_guard<std::mutex > lk(m1);

			// Проверка есть открыт ли уже файл
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


		File* Create(const char *name) {	
			std::lock_guard<std::mutex > lk(m1);
			std::unique_lock<std::shared_timed_mutex> lock(mutex);

			// Проверка есть открыт ли уже файл
			if (openedFiles.find(name) != openedFiles.end())
				if(openedFiles[name]->fState == fileState::Read)
					return nullptr; 
				else {
					openedFiles[name]->writeBuff.clear();
					return openedFiles[name];
				}
					
			auto it = std::ranges::find(nameList, name, &std::pair<std::string, size_t>::first);
			
			// Удаление данных, если файл был уже раннее создан и закрыт, перезапись главного файла
			if(it != nameList.end()) {
				std::ifstream oldF("mainFile.data", std::ios::binary);
				std::ofstream newF("mainFileTemp.data", std::ios::binary);
				
				size_t before = (*it).second;
				char c;

				for (size_t i = 0; i < before; i++) {
					oldF.get(c);
					newF << c;
				}

				oldF.seekg(0, std::ios::end);
				size_t length = oldF.tellg(); 

				it = nameList.erase(it);

				if (it != nameList.end()) {
					size_t after = (*it).second;

					oldF.seekg(after);
					
					while(oldF.get(c))
						newF << c;
					
					// Сдвиг всех адресов на размер удаляемого файла
					while (it != nameList.end()) {
						(*it).second = (*it).second - after + before;
						++it;
					}
				}

				oldF.close();
				newF.close();
				fs::remove("mainFile.data");
				fs::rename("mainFileTemp.data", "mainFile.data");
			}

			File* f = new File(name , fileState::Write);
			openedFiles[name] = f;
			return f;
		}


		size_t Read(File *f, char *buff, size_t len) {
			
			if (f->fState != fileState::Read)
				return 0;

			std::shared_lock<std::shared_timed_mutex> lock(mutex);

			f->fileStream.open("mainFile.data", std::fstream::in | std::fstream::binary);
			f->fileStream.seekg(nameList[f->index].second);
			f->fileStream.read(buff, len);
			f->fileStream.close();
			return f->fileStream.gcount();
		}


		// Сохраняет данные из buff в File, чтобы потом записать, нужно для упорядочивания записи
		size_t Write(File *f, char *buff, size_t len) {
			if (f->fState != fileState::Write)
				return 0;

			std::lock_guard<std::mutex > lk(m2);

			size_t before = f->writeBuff.size(); 
			f->writeBuff += buff;

			return  f->writeBuff.size() - before;
		}
		

		// Реально записывает данные в файл, если что-то записывалось и закрывает его
		void Close(File *f) {	
			std::lock_guard<std::mutex > lk(m2);

			if (f->fState == fileState::Write) {
				nameList.push_back(std::pair(f->filename, std::fstream("mainFile.data", std::fstream::in | std::fstream::ate | std::fstream::binary).tellg()));
				f->fileStream.open("mainFile.data", std::fstream::app | std::fstream::binary);
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
		
		
		// TestTask::VFS vfs;

		// TestTask::File* f1;
		// TestTask::File* f2;

		// TestTask::File* f3;
		// TestTask::File* f4;
		
		// std::string s1 = "SENTENCE1 ";
		// std::string s2 = "SENTENCE2 ";
		// size_t i;

		// std::string s4;
		// std::string s3;
		// std::string s5;
		// std::string s6;

		// for (size_t i = 0; i < 500; i++)
		// {
		// 	f1 = vfs.Open(("new1.txt" + std::to_string(i)).data());
		// 	// f1 = vfs.Create(("new1.txt" + std::to_string(i)).data());
		// 	f2 = vfs.Create(("new2.txt" + std::to_string(i)).data());

		// 	f3 = vfs.Open(("new1.txt" + std::to_string(i + 1)).data());
		// 	f4 = vfs.Create(("new2.txt" + std::to_string(i + 1)).data());

		// 	s3 = (s1 + std::to_string(i) + "\n");
		// 	s4 = (s2 + std::to_string(i) + "\n");
		// 	s5 = (s1 + std::to_string(i + 1) + "\n");
		// 	s6 = (s2 + std::to_string(i + 1) + "\n");
			
		// 	char* buff = new char[s3.size() + 1] {};
		// 	char* buff2 = new char[s5.size() + 1] {};

		// 	vfs.Read(f1, buff, s3.size());
			
		// 	// vfs.Write(f1, s3.data(), s3.size());

		// 	vfs.Write(f2, s4.data(), s4.size());

		// 	vfs.Read(f3, buff2, s5.size());


		// 	vfs.Write(f4, s6.data(), s6.size());
			
		// 	vfs.Close(f1);
		// 	vfs.Close(f2);
		// 	vfs.Close(f3);
		// 	vfs.Close(f4);
		// 	assert(strcmp(buff, s3.data()) == 0);
		// 	assert(strcmp(buff2, s5.data()) == 0);
		// 	delete[] buff;
		// 	delete[] buff2;
		// }

	}