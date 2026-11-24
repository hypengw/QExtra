local lito = require("@lito")
local qt = require("@lito.qt")

local qextra = lito.target({ kind = "lib", name = "qextra" })
local qt6 = lito.external_dependency(qextra, "qt6")

qt.qml_module({
  target = qextra,
  qt = qt6,
  uri = "QExtra",
  version = "1.0",
  plugin = "static",
  qml_files = {},
  moc_files = {
    { source = "include/QExtra/image.hpp", mode = "separate", output = "QExtra/moc_image.cpp" },
  },
})

qt.moc({
  target = qextra,
  qt = qt6,
  files = {
    { source = "src/async.cppm", mode = "module-split", output = "QExtra/async" },
    { source = "src/query.cppm", mode = "module-split", output = "QExtra/query" },
    {
      source = "src/select_storage.cppm",
      mode = "module-split",
      output = "QExtra/select_storage",
    },
    {
      source = "src/kstore/meta_role.cppm",
      mode = "module-split",
      output = "kstore/meta_role",
    },
    {
      source = "src/kstore/meta_list_model.cppm",
      mode = "module-split",
      output = "kstore/meta_list_model",
    },
    {
      source = "src/kstore/qtable_proxy_model.cppm",
      mode = "module-split",
      output = "kstore/qtable_proxy_model",
    },
    {
      source = "src/kstore/qunion_list_model.cppm",
      mode = "module-split",
      output = "kstore/qunion_list_model",
    },
  },
})
