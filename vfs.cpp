// Требуется С++17

#include <filesystem>
#include <iostream>
#include <mutex>
#include <fstream>
#include <thread>

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
		fs::perms originalPerms;
		fileState fState = fileState::Undef;

		File(const std::string &name, std::ios_base::openmode mode)
			{
				filename = name;
				originalPerms = fs::status(name).permissions();
				fileStream.open(filename, mode);

				if (!fileStream.is_open())
					return;

				// fs::permissons можно закоментировать, они только для того чтобы изменять атрибуты на уровне системы и не взаимодействуют с остальной vfs
				if (mode == std::fstream:: in)
				{
					fs::permissions(filename, fs::perms::group_read | fs::perms::others_read | fs::perms::owner_read, fs::perm_options::replace);
					std::ofstream(name + "readlock");
					fState = fileState::Read;
				}
				else
				{
					fs::permissions(filename, fs::perms::group_write | fs::perms::others_write | fs::perms::owner_write, fs::perm_options::replace);
					std::ofstream(name + "writelock");
					fState = fileState::Write;
				}
			}

			~File()
			{
				switch (fState)
				{
					case fileState::Read:
						fs::remove(fs::path(filename + "readlock"));
						break;

					case fileState::Write:
						fs::remove(fs::path(filename + "writelock"));
						break;

					default:
						return;
				}

				fs::permissions(filename, originalPerms);
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
		std::mutex m1, m2;

		public:

		File* Open(const char *name)
		{
			std::lock_guard<std::mutex > lk(m1);
			std::string nameS(name);

			if (fs::exists(fs::path(nameS + "writelock")))
				return nullptr;

			File *f = new File(nameS, std::fstream:: in);
			if (f->fileStream.is_open())
				return f;

			delete f;
			return nullptr;
		}

		File* Create(const char *name)
		{
			std::lock_guard<std::mutex > lk(m1);
			std::string nameS(name);

			if (fs::exists(fs::path(nameS + "readlock")))
				return nullptr;

			fs::path p(name);
			if (p.has_parent_path())
				fs::create_directories(p.parent_path());

			std::ofstream _(nameS);
			File *f = new File(nameS, std::fstream::app);
			if (f->fileStream.is_open())
				return f;

			delete f;
			return nullptr;
		}

		/**Повторные вызовы функции для одного File будут продолжать чтение с момента остановки.
		 *Пример использования функции: char s[212]; vfs.Read(f, s, 211); */
		size_t Read(File *f, char *buff, size_t len)
		{
			if (f->fState != fileState::Read)
				return 0;

			f->fileStream.read(buff, len);
			return f->fileStream.gcount();
		}


		size_t Write(File *f, char *buff, size_t len)
		{
			if (f->fState != fileState::Write)
				return 0;

			std::lock_guard<std::mutex > lk(m2);
			size_t before = fs::file_size(f->filename);

			if (!(f->fileStream.write(buff, len)))
				return 0;

			/*Следущие расточительные операции нужны для того, чтобы запись на самом деле случилась и вывод был реальным кол-вом записанных байт, 
			иначе сам файл бы изменился только после закрытия fileStream в методе Close()*/ 
			f->fileStream.close();
			f->fileStream.open(f->filename, std::fstream::app);

			return  fs::file_size(f->filename) - before;
		}

		void Close(File *f)
		{
			delete f;
		}
	};

}