#/bin/bash -x


log_file="231125-194148.log"

rm -f ./_*.csv

data_dir="${PWD}"
echo $data_dir

pushd ../../src

python log_processor.py --input_file ${data_dir}/${log_file} --output_dir ${data_dir}

python data_collector.py \
    --input_dir ${data_dir} \
    --csv_output_file ${data_dir}/_collected_tests.csv \
    --no-group_by_test \
    --channels_selection_regex "pw1"
