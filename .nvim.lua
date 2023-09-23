--[[ 
    parallel note:
    parameter order: prog ::: port1 port2 ::: targetPort1 targetPort2 
]]
vim.keymap.set("n", "<",
    [[<cmd>tabnew term://cd build && make && parallel --ungroup --tag --link ./networking ::: 3000 4000 ::: 4000 3000 ::: 127.0.0.1 127.0.0.1 ::: 100 100<CR>]]
);
