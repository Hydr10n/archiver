#ifndef _HYDR10N_FILE_ARCHIVER_H
#define _HYDR10N_FILE_ARCHIVER_H

#include <fstream>

#include <filesystem>

#include "xxhash.h"

namespace Hydr10n {
	namespace File {
		class archiver {
		public:
			archiver(const archiver&) = delete;
			archiver& operator=(const archiver&) = delete;

			struct header {
				union {
					uint32_t num;
					char str[4]{ '.', 'a', 'r', 'c' };
				} magic_number;

				XXH64_hash_t hash{};

				struct {
					uint32_t dir_count, file_count;
					uint64_t file_size;
				} stats{};
			};

			struct file_basic_info {
				bool is_dir{};

				uint64_t file_size{};

				std::filesystem::file_time_type last_write_time;

				file_basic_info() = default;

				file_basic_info(const std::filesystem::directory_entry& dir_entry) : is_dir(dir_entry.is_directory()),
					file_size(is_dir ? 0 : dir_entry.file_size()), last_write_time(dir_entry.last_write_time()) {}
			};

			struct file_node {
				file_basic_info basic_info;

				uint16_t name_size{};

				uint64_t next_sibling_offset{}, first_child_offset{};

				file_node() = default;

				file_node(const file_basic_info& src) : basic_info(src) {}
			};

			class find_file {
			public:
				struct data : file_basic_info {
					std::string filename;

					uint64_t pos{};

					data() = default;

					data(const file_node& src) noexcept : file_basic_info(src.basic_info) {}

					data(const std::filesystem::directory_entry& dir_entry) : file_basic_info(dir_entry) {
						const auto& path = dir_entry.path();
						const auto filename = path.filename().u8string();
						this->filename = filename == "" ? path.parent_path().filename().u8string() : filename;
					}
				};

				find_file(const archiver& archiver, const std::filesystem::path& path, find_file::data& data, uint64_t file_node_pos = sizeof(header));

				bool find_next(data& data);

			protected:
				const archiver& m_archiver;

				file_node m_file_node;

				void open_file(std::ifstream& file) const {
					file.exceptions(std::ios_base::badbit | std::ios_base::eofbit | std::ios_base::failbit);
					file.open(m_archiver.get_path(), std::ios_base::binary);
				}

				void read_node(std::ifstream& file, data& data) {
					file.read(reinterpret_cast<char*>(&m_file_node), sizeof(m_file_node));

					data = decltype(data)(m_file_node);

					data.filename.resize(m_file_node.name_size, 0);
					file.read((char*)(data.filename.c_str()), m_file_node.name_size);

					data.pos = static_cast<decltype(data.pos)>(file.tellg());
				}
			};

			using error_occured_event_handler = bool(const std::filesystem::path& path, void* param);

			using file_found_event_handler = bool(const std::filesystem::path& path, const find_file::data& data, void* param);

			using enter_dir_event_handler = file_found_event_handler;

			using leave_dir_event_handler = file_found_event_handler;

			archiver(const std::filesystem::path& path) : m_path(path) {
				using namespace std;

				m_file.exceptions(ios_base::badbit | ios_base::eofbit | ios_base::failbit);

				if (exists(path)) {
					if (!verify()) throw runtime_error("archiver: invalid file");

					m_file.open(path, ios_base::binary | ios_base::in | ios_base::out);
				}
			}

			virtual ~archiver() = default;

			const std::filesystem::path& get_path() const noexcept { return m_path; }

			void get_header(header& header) const { std::ifstream(m_path, std::ios_base::binary).read((char*)(&header), sizeof(header)); }

			bool verify() {
				header header;
				get_header(header);
				if (header.magic_number.num != decltype(header)().magic_number.num) return false;

				XXH64_hash_t hash;
				return m_verified = this->hash(hash) && hash == header.hash;
			}

			bool hash(XXH64_hash_t& hash) const;

			bool archive(const std::filesystem::path& path, error_occured_event_handler error_occured, file_found_event_handler file_found, enter_dir_event_handler enter_dir, void* param);

			bool find_files(const std::filesystem::path& path, error_occured_event_handler error_occured, file_found_event_handler file_found, enter_dir_event_handler enter_dir, leave_dir_event_handler leave_dir, void* param) const;

		protected:
			void write_node(const find_file::data& data) {
				file_node node = data;
				node.name_size = static_cast<decltype(node.name_size)>(data.filename.size());
				m_file.write((char*)(&node), sizeof(node)).write((char*)(data.filename.c_str()), node.name_size);
			}

			bool m_verified{};

			std::filesystem::path m_path;

			std::fstream m_file;
		};
	}
}

#endif
