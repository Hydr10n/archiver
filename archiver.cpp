#include "archiver.hpp"

#include <vector>

#include <stack>

using namespace std;
using namespace std::filesystem;

#define STOP() { errno = ECANCELED; return false; }

#define DELETE() { m_file.close(); remove(m_path); }

#define DELETE_STOP() { DELETE(); STOP(); }

constexpr int EEMPTY = ~ENOTEMPTY;

template <class T>
vector<basic_string<T>> split_path(const basic_string<T>& path) {
	vector<basic_string<T>> filenames;
	basic_string<T> temp;
	for (const auto ch : path) {
		if (ch != static_cast<T>(filesystem::path::preferred_separator))
			temp.push_back(ch);
		else if (!temp.empty()) {
			filenames.push_back(temp);
			temp.clear();
		}
	}
	if (!temp.empty())
		filenames.push_back(temp);
	return filenames;
};

namespace Hydr10n {
	namespace File {
		bool archiver::hash(XXH64_hash_t& hash) const {
			ifstream file(m_path, ios_base::binary);
			file.seekg(0, ios_base::end);
			const auto file_size = static_cast<uint64_t>(file.tellg());
			if (file_size <= sizeof(archiver::header)) return false;

			struct XXH64_wrapper {
				XXH64_state_t* state = XXH64_createState();

				~XXH64_wrapper() { XXH64_freeState(state); }
			} wrapper;
			if (wrapper.state != nullptr) {
				const auto size = static_cast<size_t>(min(file_size - offsetof(header, stats), static_cast<uint64_t>(1) << 20));
				const auto buf = make_unique<char[]>(size);
				if (XXH64_reset(wrapper.state, 0) != XXH_ERROR) {
					file.seekg(offsetof(header, stats));
					for (uint64_t pos; (pos = file.tellg()) != file_size;) {
						const auto read_size = static_cast<size_t>(file_size - pos < size ? file_size - pos : size);
						file.read(buf.get(), read_size);
						if (XXH64_update(wrapper.state, buf.get(), read_size) == XXH_ERROR) return false;
					}

					hash = XXH64_digest(wrapper.state);

					return true;
				}
			}

			return false;
		}

		archiver::find_file::find_file(const archiver& archiver, const filesystem::path& path, find_file::data& data, uint64_t file_node_pos) : m_archiver(archiver) {
			const auto throw_exception = [](int code) {
				errno = code;

				throw exception();
			};

			const auto new_path = path.u8string();
			if (new_path.empty()) throw_exception(ENOENT);

			ifstream file;
			open_file(file);

			file.seekg(file_node_pos);

			read_node(file, data);

			const auto filenames = split_path(new_path);
			const auto size = filenames.size();
			for (size_t i = 0; i < size; i++) {
				bool equal;
				while (!(equal = data.filename == filenames[i]) && find_next(data))
					;
				if (!equal) throw_exception(ENOENT);

				if (i < size - 1 || new_path.back() == filesystem::path::preferred_separator) {
					if (m_file_node.first_child_offset) {
						file.seekg(data.pos - sizeof(file_node) - m_file_node.name_size + m_file_node.first_child_offset);

						read_node(file, data);
					}
					else throw_exception(i < size - 1 || !data.is_dir ? ENOENT : EEMPTY);
				}
			}
		}

		bool archiver::find_file::find_next(data& data) {
			ifstream file;
			open_file(file);

			if (m_file_node.next_sibling_offset) {
				file.seekg(data.pos - sizeof(file_node) - m_file_node.name_size + m_file_node.next_sibling_offset);

				read_node(file, data);

				return true;
			}

			return false;
		}

		bool archiver::archive(const path& path, error_occurred_event_handler error_occurred, file_found_event_handler file_found, enter_dir_event_handler enter_dir, void* param) {
			const directory_entry dir_entry(path);
			if (!dir_entry.exists()) {
				errno = ENOENT;

				return false;
			}

			const auto parent_path = m_path.parent_path();
			if (parent_path != "") create_directories(parent_path);

			if (m_file.is_open()) m_file.close();
			m_file.open(m_path, ios_base::binary | ios_base::in | ios_base::out | ios_base::trunc);

			m_verified = false;

			header header;

			m_file.seekp(sizeof(header));

			try {
				if (dir_entry.is_directory()) {
					header.stats.dir_count = 1;

					struct node {
						int depth;

						uint64_t next_sibling_offset_pos, first_child_offset_pos;
					};

					stack<node> nodes;

					const auto& path = dir_entry.path();
					if (path.is_absolute() && path != path.root_path()) {
						write_node(find_file::data(dir_entry));

						nodes.push({ -1, 0, offsetof(file_node, first_child_offset) + sizeof(header) });
					}

					const recursive_directory_iterator iterator(path, directory_options::skip_permission_denied);
					for (const auto& dir_entry : iterator) {
						const auto is_dir = dir_entry.is_directory();
						if (is_dir || dir_entry.is_regular_file()) {
							if (!is_dir && equivalent(dir_entry.path(), m_path))
								continue;

							const auto node_pos = static_cast<uint64_t>(m_file.tellg());

							find_file::data data(dir_entry);
							data.pos = node_pos + sizeof(file_node) + data.filename.size() + data.file_size;

							if (is_dir) {
								header.stats.dir_count++;

								if (enter_dir != nullptr && !enter_dir(dir_entry.path().parent_path() / "", data, param)) DELETE_STOP();
							}
							else {
								header.stats.file_count++;
								header.stats.file_size += data.file_size;

								if (file_found != nullptr && !file_found(dir_entry.path().parent_path() / "", data, param)) DELETE_STOP();
							}

							const auto new_depth = iterator.depth();

							if (!nodes.empty()) {
								const auto& top = nodes.top();
								uint64_t offset;
								if (new_depth > top.depth) {
									offset = node_pos + offsetof(file_node, first_child_offset) - top.first_child_offset_pos;

									m_file.seekp(top.first_child_offset_pos ? top.first_child_offset_pos : top.next_sibling_offset_pos);
								}
								else {
									uint64_t next_sibling_offset_pos;
									if (new_depth < top.depth) {
										for (;;) {
											const auto& top = nodes.top();
											if (top.depth != new_depth) nodes.pop();
											else {
												next_sibling_offset_pos = top.next_sibling_offset_pos;

												offset = node_pos + offsetof(file_node, next_sibling_offset) - top.next_sibling_offset_pos;

												break;
											}
										}
									}
									else {
										next_sibling_offset_pos = top.next_sibling_offset_pos;

										offset = node_pos + offsetof(file_node, next_sibling_offset) - top.next_sibling_offset_pos;
									}

									m_file.seekp(next_sibling_offset_pos);
								}

								m_file.write((char*)(&offset), sizeof(offset));
							}

							m_file.seekp(node_pos);
							write_node(data);

							nodes.push({ new_depth, offsetof(file_node, next_sibling_offset) + node_pos, offsetof(file_node, first_child_offset) + node_pos });
						}
					}
				}
				else {
					const auto node_pos = static_cast<uint64_t>(m_file.tellg());

					find_file::data data(dir_entry);
					data.pos = node_pos + sizeof(file_node) + data.filename.size() + data.file_size;

					header.stats.file_count = 1;
					header.stats.file_size = data.file_size;

					if (file_found != nullptr && !file_found(dir_entry.path().parent_path() / "", data, param)) DELETE_STOP();

					write_node(data);
				}

				m_file.seekp(0);
				m_file.write((char*)(&header), sizeof(header));

				m_file.flush();

				hash(header.hash);

				m_file.seekp(offsetof(decltype(header), hash));
				m_file.write((char*)(&header.hash), sizeof(header.hash));

				m_verified = true;
			}
			catch (...) {
				DELETE();

				return false;
			}

			return true;
		}

		bool archiver::find_files(const path& path, error_occurred_event_handler error_occurred, file_found_event_handler file_found, enter_dir_event_handler enter_dir, leave_dir_event_handler leave_dir, void* param) const {
			if (!m_verified) {
				errno = ENOENT;

				return false;
			}

			auto new_path = (path / "").u8string();

			const auto find_files = [&](const auto& find_files, uint64_t file_node_pos, uint32_t depth, error_occurred_event_handler error_occurred, file_found_event_handler file_found, enter_dir_event_handler enter_dir, leave_dir_event_handler leave_dir, void* param) {
				try {
					const auto new_path_size = new_path.size();

					find_file::data data;

					find_file find(*this, depth ? initializer_list<char>({ path::preferred_separator, 0 }).begin() : path, data, file_node_pos);

					do {
						if (data.is_dir) {
							if (enter_dir != nullptr && !enter_dir(new_path, data, param)) STOP();

							new_path += data.filename + static_cast<char>(path::preferred_separator);

							if (!find_files(find_files, data.pos, depth + 1, error_occurred, file_found, enter_dir, leave_dir, param)) return false;

							new_path.erase(new_path_size);

							if (leave_dir != nullptr && !leave_dir(new_path, data, param)) STOP();
						}
						else if (file_found != nullptr && !file_found(new_path, data, param)) STOP();
					} while (find.find_next(data));
				}
				catch (const ios_base::failure&) {
					if (error_occurred != nullptr) error_occurred(path, param);
					return false;
				}
				catch (...) {
					if (errno && (errno != EEMPTY || !depth)) {
						if (error_occurred != nullptr) error_occurred(path, param);
						return false;
					}
				}

				return true;
			};

			return find_files(find_files, sizeof(header), 0, error_occurred, file_found, enter_dir, leave_dir, param);
		}
	}
}
